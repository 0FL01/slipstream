//! In-process loopback UDP <-> DNS-over-TCP bridge to one recursive resolver.
//!
//! The QUIC runtime keeps speaking authoritative-mode UDP to a loopback endpoint;
//! this bridge owns a dedicated thread with its own Tokio runtime and `lanes`
//! persistent TCP connections. Each lane keeps up to `depth` exchanges in flight,
//! matches answers by DNS ID (QR set), reconnects off the query path when the
//! resolver closes the connection, and re-queues unanswered queries once.

use std::collections::{HashMap, VecDeque};
use std::io;
use std::net::{SocketAddr, UdpSocket as StdUdpSocket};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::tcp::OwnedReadHalf;
use tokio::net::{TcpStream, UdpSocket};
use tokio::sync::{mpsc, Notify};

const QUERY_MAX_AGE: Duration = Duration::from_secs(10);
const CONNECT_TIMEOUT: Duration = Duration::from_secs(8);
const EXCHANGE_TIMEOUT: Duration = Duration::from_secs(5);
const RECONNECT_MIN: Duration = Duration::from_millis(100);
const RECONNECT_MAX: Duration = Duration::from_secs(2);
const MAX_ATTEMPTS: u8 = 2;
const MAX_DNS_MESSAGE: usize = 4096;
/// Keep this many lanes permanently connected; additional lanes exist only
/// while the queue is deep and close again after being idle.
const LANE_MIN: usize = 8;
const LANE_IDLE: Duration = Duration::from_secs(30);

/// Aggregate active lanes across all bridge instances, for aggregate-only DPI
/// stats. No per-lane state is exposed.
static ACTIVE_LANES: AtomicUsize = AtomicUsize::new(0);

pub(crate) fn active_lanes() -> usize {
    ACTIVE_LANES.load(Ordering::Relaxed)
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) struct BridgeConfig {
    pub(crate) lanes: usize,
    pub(crate) depth: usize,
    pub(crate) queue: usize,
}

struct Query {
    data: Vec<u8>,
    src: SocketAddr,
    born: Instant,
    attempts: u8,
}

impl Query {
    fn id(&self) -> u16 {
        u16::from_be_bytes([self.data[0], self.data[1]])
    }
    fn fresh(&self, now: Instant) -> bool {
        now.duration_since(self.born) < QUERY_MAX_AGE
    }
}

struct Shared {
    queue: Mutex<VecDeque<Query>>,
    notify: Notify,
    capacity: usize,
}

impl Shared {
    /// Enqueue a new query; a full queue drops the newest query (unchanged policy).
    fn push_back(&self, query: Query) -> bool {
        let mut queue = self.queue.lock().unwrap();
        let now = Instant::now();
        while queue.front().is_some_and(|q| !q.fresh(now)) {
            queue.pop_front();
        }
        if queue.len() >= self.capacity {
            return false;
        }
        queue.push_back(query);
        drop(queue);
        self.notify.notify_one();
        true
    }

    /// Re-queue an unanswered query ahead of new work.
    fn push_front(&self, query: Query) {
        self.queue.lock().unwrap().push_front(query);
        self.notify.notify_one();
    }

    fn pop(&self) -> Option<Query> {
        let mut queue = self.queue.lock().unwrap();
        let now = Instant::now();
        while let Some(query) = queue.pop_front() {
            if query.fresh(now) {
                return Some(query);
            }
        }
        None
    }
}

struct LanePool {
    shared: Arc<Shared>,
    udp: Arc<UdpSocket>,
    resolver: SocketAddr,
    depth: usize,
    max_lanes: usize,
    active: AtomicUsize,
}

impl LanePool {
    fn spawn(self: &Arc<Self>, persistent: bool) {
        if self.active.fetch_add(1, Ordering::AcqRel) >= self.max_lanes {
            self.active.fetch_sub(1, Ordering::AcqRel);
            return;
        }
        ACTIVE_LANES.fetch_add(1, Ordering::Relaxed);
        let pool = Arc::clone(self);
        tokio::spawn(async move {
            lane(pool.clone(), persistent).await;
            pool.active.fetch_sub(1, Ordering::AcqRel);
            ACTIVE_LANES.fetch_sub(1, Ordering::Relaxed);
            pool.ensure_capacity();
        });
    }

    /// Add one lane when the queue exceeds the work already held by the
    /// currently connected lanes. Repeated arrivals ramp up to `max_lanes`.
    fn ensure_capacity(self: &Arc<Self>) {
        let queued = self.shared.queue.lock().unwrap().len();
        let active = self.active.load(Ordering::Acquire);
        if queued > active.saturating_mul(self.depth) {
            self.spawn(false);
        }
    }
}

/// Binds the loopback endpoint and starts the bridge thread. Returns the loopback
/// address the QUIC runtime must use as its authoritative resolver.
pub(crate) fn spawn(resolver: SocketAddr, config: BridgeConfig) -> io::Result<SocketAddr> {
    if config.lanes == 0 || config.depth == 0 || config.queue == 0 {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "TCP bridge lanes, depth and queue must be positive",
        ));
    }
    let udp = StdUdpSocket::bind(("127.0.0.1", 0))?;
    udp.set_nonblocking(true)?;
    let local = udp.local_addr()?;
    std::thread::Builder::new()
        .name("dns-tcp-bridge".into())
        .spawn(move || {
            let runtime = match tokio::runtime::Builder::new_current_thread()
                .enable_io()
                .enable_time()
                .build()
            {
                Ok(runtime) => runtime,
                Err(err) => {
                    tracing::error!("TCP DNS bridge runtime error: {}", err);
                    std::process::exit(1);
                }
            };
            if let Err(err) = runtime.block_on(run(udp, resolver, config)) {
                tracing::error!("TCP DNS bridge error: {}", err);
                std::process::exit(1);
            }
        })?;
    Ok(local)
}

async fn run(udp: StdUdpSocket, resolver: SocketAddr, config: BridgeConfig) -> io::Result<()> {
    let udp = Arc::new(UdpSocket::from_std(udp)?);
    let shared = Arc::new(Shared {
        queue: Mutex::new(VecDeque::with_capacity(config.queue)),
        notify: Notify::new(),
        capacity: config.queue,
    });
    let pool = Arc::new(LanePool {
        shared: shared.clone(),
        udp: udp.clone(),
        resolver,
        depth: config.depth,
        max_lanes: config.lanes,
        active: AtomicUsize::new(0),
    });
    for _ in 0..config.lanes.min(LANE_MIN) {
        pool.spawn(true);
    }
    let mut buf = vec![0u8; MAX_DNS_MESSAGE];
    loop {
        let (len, src) = match udp.recv_from(&mut buf).await {
            Ok(value) => value,
            Err(err) if slipstream_core::net::is_transient_udp_error(&err) => continue,
            Err(err) => return Err(err),
        };
        if len < 12 {
            continue;
        }
        shared.push_back(Query {
            data: buf[..len].to_vec(),
            src,
            born: Instant::now(),
            attempts: 0,
        });
        pool.ensure_capacity();
    }
}

enum Event {
    Response(Option<Vec<u8>>),
    Work,
    Tick,
    Idle,
}

enum ServeOutcome {
    Closed(Option<usize>),
    Idle,
}

async fn lane(pool: Arc<LanePool>, persistent: bool) {
    let mut backoff = RECONNECT_MIN;
    loop {
        let stream =
            match tokio::time::timeout(CONNECT_TIMEOUT, TcpStream::connect(pool.resolver)).await {
                Ok(Ok(stream)) => stream,
                _ => {
                    tokio::time::sleep(backoff).await;
                    backoff = (backoff * 2).min(RECONNECT_MAX);
                    continue;
                }
            };
        backoff = RECONNECT_MIN;
        let _ = stream.set_nodelay(true);
        let (reader, writer) = stream.into_split();
        let (tx, rx) = mpsc::channel(pool.depth + 4);
        let reader_task = tokio::spawn(read_frames(reader, tx));
        let mut pending = HashMap::with_capacity(pool.depth);
        let outcome = serve(
            &pool.shared,
            &pool.udp,
            writer,
            rx,
            pool.depth,
            &mut pending,
            persistent,
        )
        .await;
        reader_task.abort();
        // A resolver closing a connection that was answering (normal churn) is not
        // the query's fault; only silent or dead connections consume an attempt.
        let answered = match outcome {
            ServeOutcome::Idle => return,
            ServeOutcome::Closed(answered) => answered,
        };
        let charge = u8::from(answered.is_none());
        let now = Instant::now();
        for (_, (mut query, _)) in pending.drain() {
            query.attempts += charge;
            if query.attempts < MAX_ATTEMPTS && query.fresh(now) {
                pool.shared.push_front(query);
            }
        }
    }
}

/// Runs one connection until it breaks; unanswered queries remain in `pending`.
/// Returns `Some(answers)` when the peer closed after answering, `None` when the
/// connection timed out, failed to write, or closed without any answer.
async fn serve(
    shared: &Shared,
    udp: &UdpSocket,
    mut writer: tokio::net::tcp::OwnedWriteHalf,
    mut rx: mpsc::Receiver<Vec<u8>>,
    depth: usize,
    pending: &mut HashMap<u16, (Query, Instant)>,
    persistent: bool,
) -> ServeOutcome {
    let mut frame = Vec::with_capacity(MAX_DNS_MESSAGE + 2);
    let mut answers = 0usize;
    loop {
        while pending.len() < depth {
            let Some(query) = shared.pop() else { break };
            frame.clear();
            frame.extend_from_slice(&(query.data.len() as u16).to_be_bytes());
            frame.extend_from_slice(&query.data);
            let id = query.id();
            if writer.write_all(&frame).await.is_err() {
                pending.insert(id, (query, Instant::now()));
                return ServeOutcome::Closed((answers > 0).then_some(answers));
            }
            // A duplicate ID on this lane keeps only the newest exchange.
            pending.insert(id, (query, Instant::now()));
        }
        let oldest = pending.values().map(|(_, sent)| *sent).min();
        let tick = async {
            match oldest {
                Some(sent) => tokio::time::sleep_until((sent + EXCHANGE_TIMEOUT).into()).await,
                None => std::future::pending::<()>().await,
            }
        };
        let idle = async {
            if persistent {
                std::future::pending::<()>().await
            } else {
                tokio::time::sleep(LANE_IDLE).await
            }
        };
        let event = if pending.is_empty() && !persistent {
            tokio::select! {
                biased;
                response = rx.recv() => Event::Response(response),
                _ = shared.notify.notified() => Event::Work,
                _ = idle => Event::Idle,
            }
        } else if pending.len() < depth {
            tokio::select! {
                biased;
                response = rx.recv() => Event::Response(response),
                _ = shared.notify.notified() => Event::Work,
                _ = tick => Event::Tick,
            }
        } else {
            tokio::select! {
                biased;
                response = rx.recv() => Event::Response(response),
                _ = tick => Event::Tick,
            }
        };
        match event {
            Event::Response(None) => return ServeOutcome::Closed((answers > 0).then_some(answers)),
            Event::Tick => return ServeOutcome::Closed(None),
            Event::Idle => return ServeOutcome::Idle,
            Event::Work => {}
            Event::Response(Some(response)) => {
                if response[2] & 0x80 == 0 {
                    continue;
                }
                let id = u16::from_be_bytes([response[0], response[1]]);
                if let Some((query, _)) = pending.remove(&id) {
                    answers += 1;
                    let _ = udp.send_to(&response, query.src).await;
                }
            }
        }
    }
}

async fn read_frames(mut reader: OwnedReadHalf, tx: mpsc::Sender<Vec<u8>>) {
    loop {
        let mut len = [0u8; 2];
        if reader.read_exact(&mut len).await.is_err() {
            return;
        }
        let len = u16::from_be_bytes(len) as usize;
        if !(12..=MAX_DNS_MESSAGE).contains(&len) {
            return;
        }
        let mut message = vec![0u8; len];
        if reader.read_exact(&mut message).await.is_err() {
            return;
        }
        if tx.send(message).await.is_err() {
            return;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tokio::net::TcpListener;

    fn query(id: u16) -> Vec<u8> {
        let mut q = vec![0u8; 12];
        q[..2].copy_from_slice(&id.to_be_bytes());
        q[5] = 1;
        q
    }

    /// Fake resolver: answers each batch (optionally in reverse order) and closes
    /// every connection after `per_conn` answers (Yandex-like churn).
    async fn fake_resolver(per_conn: usize, reverse: bool) -> SocketAddr {
        let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
        let addr = listener.local_addr().unwrap();
        tokio::spawn(async move {
            loop {
                let (mut s, _) = listener.accept().await.unwrap();
                tokio::spawn(async move {
                    let mut answered = 0;
                    let mut batch = Vec::new();
                    loop {
                        let mut len = [0u8; 2];
                        let read = tokio::time::timeout(
                            Duration::from_millis(20),
                            s.read_exact(&mut len),
                        )
                        .await;
                        match read {
                            Ok(Ok(_)) => {
                                let mut m = vec![0u8; u16::from_be_bytes(len) as usize];
                                s.read_exact(&mut m).await.unwrap();
                                m[2] |= 0x80;
                                batch.push(m);
                            }
                            Ok(Err(_)) => return,
                            Err(_) => {
                                if !reverse {
                                    batch.reverse();
                                }
                                while let Some(m) = batch.pop() {
                                    let mut f = (m.len() as u16).to_be_bytes().to_vec();
                                    f.extend_from_slice(&m);
                                    s.write_all(&f).await.unwrap();
                                    answered += 1;
                                    if answered >= per_conn {
                                        return;
                                    }
                                }
                                batch.clear();
                            }
                        }
                    }
                });
            }
        });
        addr
    }

    async fn exchange_all(per_conn: usize, reverse: bool, depth: usize, n: u16) -> usize {
        let resolver = fake_resolver(per_conn, reverse).await;
        let bridge = spawn(resolver, BridgeConfig { lanes: 4, depth, queue: 256 }).unwrap();
        let client = UdpSocket::bind("127.0.0.1:0").await.unwrap();
        for id in 0..n {
            client.send_to(&query(id), bridge).await.unwrap();
            if id % 32 == 31 {
                tokio::time::sleep(Duration::from_millis(30)).await;
            }
        }
        let mut seen = std::collections::HashSet::new();
        let mut buf = [0u8; 512];
        while let Ok(Ok((len, _))) =
            tokio::time::timeout(Duration::from_secs(2), client.recv_from(&mut buf)).await
        {
            assert!(len >= 12 && buf[2] & 0x80 != 0);
            seen.insert(u16::from_be_bytes([buf[0], buf[1]]));
            if seen.len() == n as usize {
                break;
            }
        }
        seen.len()
    }

    #[tokio::test]
    async fn pipelined_out_of_order_answers_are_matched_by_id() {
        assert_eq!(exchange_all(usize::MAX, true, 4, 128).await, 128);
    }

    #[tokio::test]
    async fn resolver_connection_churn_loses_no_query() {
        // Every connection closes after 3 answers while 4 are in flight.
        assert_eq!(exchange_all(3, false, 4, 128).await, 128);
    }

    #[test]
    fn rejects_zero_sizes() {
        let addr: SocketAddr = "127.0.0.1:53".parse().unwrap();
        assert!(spawn(addr, BridgeConfig { lanes: 0, depth: 1, queue: 1 }).is_err());
    }
}

use std::io::{self, Read};
use tokio::io::{AsyncRead, AsyncReadExt, AsyncWrite, AsyncWriteExt};

const TOKEN_LEN: usize = 16;
const CREDENTIAL_LEN: usize = 32;
const BOOTSTRAP_LEN: usize = TOKEN_LEN + CREDENTIAL_LEN * 2;
const UOT_MAGIC: &[u8] = b"sp.v2.udp-over-tcp.arpa";

enum Address {
    Ipv4([u8; 4]),
    Ipv6([u8; 16]),
    Domain(Vec<u8>),
}

pub(crate) struct FlowRelayConfig {
    token: [u8; TOKEN_LEN],
    username: [u8; CREDENTIAL_LEN],
    password: [u8; CREDENTIAL_LEN],
}

pub(crate) fn read_bootstrap(mut input: impl Read) -> Result<FlowRelayConfig, String> {
    let mut bootstrap = [0u8; BOOTSTRAP_LEN];
    input
        .read_exact(&mut bootstrap)
        .map_err(|_| "expected an 80-byte FlowRelay bootstrap".to_string())?;
    let mut extra = [0u8; 1];
    if input.read(&mut extra).map_err(|err| err.to_string())? != 0 {
        return Err("expected an 80-byte FlowRelay bootstrap".to_string());
    }

    let mut token = [0u8; TOKEN_LEN];
    token.copy_from_slice(&bootstrap[..TOKEN_LEN]);
    let mut username = [0u8; CREDENTIAL_LEN];
    username.copy_from_slice(&bootstrap[TOKEN_LEN..TOKEN_LEN + CREDENTIAL_LEN]);
    let mut password = [0u8; CREDENTIAL_LEN];
    password.copy_from_slice(&bootstrap[TOKEN_LEN + CREDENTIAL_LEN..]);
    if !username.iter().chain(&password).all(u8::is_ascii_hexdigit)
        || username.iter().chain(&password).any(u8::is_ascii_uppercase)
    {
        return Err("FlowRelay SOCKS credentials must be lowercase hex".to_string());
    }
    Ok(FlowRelayConfig {
        token,
        username,
        password,
    })
}

impl FlowRelayConfig {
    pub(crate) fn dns_key(&self) -> [u8; 32] {
        crate::dns::derive_key(&self.token)
    }
}

pub(crate) async fn accept_socks<S>(
    stream: &mut S,
    config: &FlowRelayConfig,
) -> io::Result<Box<[u8]>>
where
    S: AsyncRead + AsyncWrite + Unpin,
{
    let mut greeting = [0u8; 2];
    stream.read_exact(&mut greeting).await?;
    if greeting[0] != 5 || !(1..=16).contains(&greeting[1]) {
        return invalid_data("invalid SOCKS greeting");
    }
    let mut methods = vec![0u8; greeting[1] as usize];
    stream.read_exact(&mut methods).await?;
    if !methods.contains(&2) {
        stream.write_all(&[5, 0xff]).await?;
        return invalid_data("SOCKS username/password authentication is required");
    }
    stream.write_all(&[5, 2]).await?;

    let mut auth = [0u8; 2];
    stream.read_exact(&mut auth).await?;
    if auth != [1, CREDENTIAL_LEN as u8] {
        return invalid_data("invalid SOCKS authentication request");
    }
    let mut username = [0u8; CREDENTIAL_LEN];
    stream.read_exact(&mut username).await?;
    let password_len = stream.read_u8().await?;
    if password_len as usize != CREDENTIAL_LEN {
        stream.write_all(&[1, 1]).await?;
        return invalid_data("invalid SOCKS credentials");
    }
    let mut password = [0u8; CREDENTIAL_LEN];
    stream.read_exact(&mut password).await?;
    if !constant_time_eq(&username, &config.username)
        || !constant_time_eq(&password, &config.password)
    {
        stream.write_all(&[1, 1]).await?;
        return invalid_data("invalid SOCKS credentials");
    }
    stream.write_all(&[1, 0]).await?;

    let mut request = [0u8; 4];
    stream.read_exact(&mut request).await?;
    if request[0] != 5 || request[2] != 0 {
        return invalid_data("invalid SOCKS request");
    }
    if request[1] != 1 {
        send_reply(stream, 7).await?;
        return invalid_data("unsupported SOCKS command");
    }

    let address = match request[3] {
        1 => {
            let mut address = [0u8; 4];
            stream.read_exact(&mut address).await?;
            Address::Ipv4(address)
        }
        4 => {
            let mut address = [0u8; 16];
            stream.read_exact(&mut address).await?;
            Address::Ipv6(address)
        }
        3 => {
            let len = stream.read_u8().await?;
            if len == 0 || len > 253 {
                send_reply(stream, 8).await?;
                return invalid_data("invalid SOCKS domain length");
            }
            let mut address = vec![0; len as usize];
            stream.read_exact(&mut address).await?;
            Address::Domain(address)
        }
        _ => {
            send_reply(stream, 8).await?;
            return invalid_data("unsupported SOCKS address type");
        }
    };
    let mut port = [0u8; 2];
    stream.read_exact(&mut port).await?;
    let is_udp = matches!(&address, Address::Domain(name) if name == UOT_MAGIC) && port == [0, 0];
    if port == [0, 0] && !is_udp {
        send_reply(stream, 1).await?;
        return invalid_data("SOCKS destination port is zero");
    }
    send_reply(stream, 0).await?;
    if is_udp {
        if stream.read_u8().await? != 0 {
            return invalid_data("only bind-mode UoT is supported");
        }
        let address_type = stream.read_u8().await?;
        let address = read_address(stream, address_type).await?;
        stream.read_exact(&mut port).await?;
        if port == [0, 0] {
            return invalid_data("UoT destination port is zero");
        }
        Ok(encode_open(config, 1, address, port))
    } else {
        Ok(encode_open(config, 0, address, port))
    }
}

async fn read_address<S: AsyncRead + Unpin>(
    stream: &mut S,
    address_type: u8,
) -> io::Result<Address> {
    match address_type {
        1 => {
            let mut address = [0u8; 4];
            stream.read_exact(&mut address).await?;
            Ok(Address::Ipv4(address))
        }
        4 => {
            let mut address = [0u8; 16];
            stream.read_exact(&mut address).await?;
            Ok(Address::Ipv6(address))
        }
        3 => {
            let len = stream.read_u8().await?;
            if len == 0 || len > 253 {
                return invalid_data("invalid UoT domain length");
            }
            let mut address = vec![0; len as usize];
            stream.read_exact(&mut address).await?;
            Ok(Address::Domain(address))
        }
        _ => invalid_data("unsupported UoT address type"),
    }
}

fn encode_open(
    config: &FlowRelayConfig,
    command: u8,
    address: Address,
    port: [u8; 2],
) -> Box<[u8]> {
    let address_type = match &address {
        Address::Ipv4(_) => 0,
        Address::Ipv6(_) => 1,
        Address::Domain(_) => 2,
    };
    let mut open = Vec::with_capacity(64);
    open.push(0x20 | command << 3 | address_type << 1);
    open.extend_from_slice(&config.token);
    match address {
        Address::Ipv4(value) => open.extend_from_slice(&value),
        Address::Ipv6(value) => open.extend_from_slice(&value),
        Address::Domain(value) => {
            open.push(value.len() as u8);
            open.extend_from_slice(&value);
        }
    }
    open.extend_from_slice(&port);
    open.into_boxed_slice()
}

async fn send_reply<S: AsyncWrite + Unpin>(stream: &mut S, reply: u8) -> io::Result<()> {
    stream.write_all(&[5, reply, 0, 1, 0, 0, 0, 0, 0, 0]).await
}

fn constant_time_eq(left: &[u8], right: &[u8]) -> bool {
    left.len() == right.len()
        && left
            .iter()
            .zip(right)
            .fold(0u8, |diff, (left, right)| diff | (left ^ right))
            == 0
}

fn invalid_data<T>(message: &'static str) -> io::Result<T> {
    Err(io::Error::new(io::ErrorKind::InvalidData, message))
}

#[cfg(test)]
mod tests {
    use super::{accept_socks, read_bootstrap, FlowRelayConfig, UOT_MAGIC};
    use tokio::io::{duplex, AsyncReadExt, AsyncWriteExt};

    fn config() -> FlowRelayConfig {
        let mut input = Vec::from([7u8; 16]);
        input.extend_from_slice(b"0123456789abcdef0123456789abcdef");
        input.extend_from_slice(b"fedcba9876543210fedcba9876543210");
        read_bootstrap(input.as_slice()).expect("valid bootstrap")
    }

    async fn open_for_address(address: &[u8]) -> Box<[u8]> {
        let (mut client, mut server) = duplex(1024);
        let mut request = vec![5, 1, 2, 1, 32];
        request.extend_from_slice(b"0123456789abcdef0123456789abcdef");
        request.push(32);
        request.extend_from_slice(b"fedcba9876543210fedcba9876543210");
        request.extend_from_slice(&[5, 1, 0]);
        request.extend_from_slice(address);
        client.write_all(&request).await.expect("SOCKS request");
        accept_socks(&mut server, &config())
            .await
            .expect("SOCKS connect")
    }

    #[test]
    fn bootstrap_is_exact_and_lowercase() {
        let mut valid = Vec::from([7u8; 16]);
        valid.extend_from_slice(b"0123456789abcdef0123456789abcdef");
        valid.extend_from_slice(b"fedcba9876543210fedcba9876543210");
        assert!(read_bootstrap(valid.as_slice()).is_ok());
        assert!(read_bootstrap(&valid[..79]).is_err());
        valid.push(0);
        assert!(read_bootstrap(valid.as_slice()).is_err());
        valid.pop();
        valid[16] = b'A';
        assert!(read_bootstrap(valid.as_slice()).is_err());
    }

    #[test]
    fn authenticated_domain_connect_encodes_flow_open() {
        let runtime = tokio::runtime::Builder::new_current_thread()
            .enable_io()
            .build()
            .expect("runtime");
        runtime.block_on(async {
            let (mut client, mut server) = duplex(1024);
            let mut request = vec![5, 1, 2, 1, 32];
            request.extend_from_slice(b"0123456789abcdef0123456789abcdef");
            request.push(32);
            request.extend_from_slice(b"fedcba9876543210fedcba9876543210");
            request.extend_from_slice(&[5, 1, 0, 3, 11]);
            request.extend_from_slice(b"example.com");
            request.extend_from_slice(&[0x01, 0xbb]);
            for byte in request {
                client.write_all(&[byte]).await.expect("fragmented write");
            }

            let open = accept_socks(&mut server, &config())
                .await
                .expect("SOCKS connect");
            let mut responses = [0u8; 14];
            client
                .read_exact(&mut responses)
                .await
                .expect("SOCKS responses");
            assert_eq!(&responses, &[5, 2, 1, 0, 5, 0, 0, 1, 0, 0, 0, 0, 0, 0]);

            let mut expected = vec![0x24];
            expected.extend_from_slice(&[7u8; 16]);
            expected.push(11);
            expected.extend_from_slice(b"example.com");
            expected.extend_from_slice(&[0x01, 0xbb]);
            assert_eq!(&*open, expected);
        });
    }

    #[test]
    fn wrong_password_is_rejected_before_connect() {
        let runtime = tokio::runtime::Builder::new_current_thread()
            .enable_io()
            .build()
            .expect("runtime");
        runtime.block_on(async {
            let (mut client, mut server) = duplex(1024);
            let mut request = vec![5, 1, 2, 1, 32];
            request.extend_from_slice(b"0123456789abcdef0123456789abcdef");
            request.push(32);
            request.extend_from_slice(b"00000000000000000000000000000000");
            client.write_all(&request).await.expect("request");
            assert!(accept_socks(&mut server, &config()).await.is_err());
            let mut responses = [0u8; 4];
            client
                .read_exact(&mut responses)
                .await
                .expect("SOCKS responses");
            assert_eq!(responses, [5, 2, 1, 1]);
        });
    }

    #[test]
    fn ipv4_and_ipv6_connects_encode_flow_open() {
        let runtime = tokio::runtime::Builder::new_current_thread()
            .enable_io()
            .build()
            .expect("runtime");
        runtime.block_on(async {
            let ipv4 = open_for_address(&[1, 127, 0, 0, 1, 0, 80]).await;
            let mut expected = vec![0x20];
            expected.extend_from_slice(&[7u8; 16]);
            expected.extend_from_slice(&[127, 0, 0, 1, 0, 80]);
            assert_eq!(&*ipv4, expected);

            let mut address = vec![4];
            address.extend_from_slice(&[0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1]);
            address.extend_from_slice(&[1, 187]);
            let ipv6 = open_for_address(&address).await;
            let mut expected = vec![0x22];
            expected.extend_from_slice(&[7u8; 16]);
            expected.extend_from_slice(&address[1..]);
            assert_eq!(&*ipv6, expected);
        });
    }

    #[test]
    fn uot_bind_becomes_udp_open_and_leaves_records_unchanged() {
        let runtime = tokio::runtime::Builder::new_current_thread()
            .enable_io()
            .build()
            .expect("runtime");
        runtime.block_on(async {
            let (mut client, mut server) = duplex(1024);
            let mut request = vec![5, 1, 2, 1, 32];
            request.extend_from_slice(b"0123456789abcdef0123456789abcdef");
            request.push(32);
            request.extend_from_slice(b"fedcba9876543210fedcba9876543210");
            request.extend_from_slice(&[5, 1, 0, 3, UOT_MAGIC.len() as u8]);
            request.extend_from_slice(UOT_MAGIC);
            request.extend_from_slice(&[0, 0]);
            request.extend_from_slice(&[0, 3, 11]);
            request.extend_from_slice(b"example.com");
            request.extend_from_slice(&[0, 53]);
            let mut record = vec![2, 11];
            record.extend_from_slice(b"example.com");
            record.extend_from_slice(&[0, 53, 0, 5]);
            record.extend_from_slice(b"nonce");
            request.extend_from_slice(&record);
            client.write_all(&request).await.expect("UoT request");

            let open = accept_socks(&mut server, &config())
                .await
                .expect("UoT bind");
            let mut forwarded = vec![0; record.len()];
            server
                .read_exact(&mut forwarded)
                .await
                .expect("FlowRelay record");
            assert_eq!(forwarded, record);

            let mut responses = [0u8; 14];
            client
                .read_exact(&mut responses)
                .await
                .expect("SOCKS responses");
            assert_eq!(&responses, &[5, 2, 1, 0, 5, 0, 0, 1, 0, 0, 0, 0, 0, 0]);

            let mut expected = vec![0x2c];
            expected.extend_from_slice(&[7u8; 16]);
            expected.push(11);
            expected.extend_from_slice(b"example.com");
            expected.extend_from_slice(&[0, 53]);
            assert_eq!(&*open, expected);
        });
    }
}

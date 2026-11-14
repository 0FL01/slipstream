package logger

// No application or carrier metadata is emitted, including on failures.
func OwenclaveSilent() *Logger { return &Logger{level: levelError + 1} }

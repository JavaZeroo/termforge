#[allow(dead_code)]
#[path = "main.rs"]
mod trace;

use std::alloc::{GlobalAlloc, Layout, System};
use std::env;
use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};
use std::time::Instant;

use alacritty_terminal::term::{Config, Term};
use alacritty_terminal::vte::ansi::Processor;
use serde_json::{Value, json};
use trace::{Listener, Size};

struct CountingAllocator;
static OBSERVE: AtomicBool = AtomicBool::new(false);
static ALLOCATIONS: AtomicUsize = AtomicUsize::new(0);
static ALLOCATED_BYTES: AtomicUsize = AtomicUsize::new(0);

unsafe impl GlobalAlloc for CountingAllocator {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        if OBSERVE.load(Ordering::Relaxed) {
            ALLOCATIONS.fetch_add(1, Ordering::Relaxed);
            ALLOCATED_BYTES.fetch_add(layout.size(), Ordering::Relaxed);
        }
        unsafe { System.alloc(layout) }
    }

    unsafe fn dealloc(&self, ptr: *mut u8, layout: Layout) {
        unsafe { System.dealloc(ptr, layout) }
    }
}

#[global_allocator]
static ALLOCATOR: CountingAllocator = CountingAllocator;

#[cfg(windows)]
mod platform {
    #[repr(C)]
    struct FileTime { low: u32, high: u32 }
    #[repr(C)]
    struct MemoryCounters {
        cb: u32, faults: u32, peak: usize, working: usize, quota_peak_paged: usize,
        quota_paged: usize, quota_peak_non_paged: usize, quota_non_paged: usize,
        pagefile: usize, peak_pagefile: usize,
    }
    #[link(name = "kernel32")]
    unsafe extern "system" {
        fn GetCurrentProcess() -> isize;
        fn GetProcessTimes(process: isize, create: *mut FileTime, exit: *mut FileTime,
                           kernel: *mut FileTime, user: *mut FileTime) -> i32;
        fn GetProcessMemoryInfo(process: isize, counters: *mut MemoryCounters, size: u32) -> i32;
    }
    pub fn cpu_seconds() -> Result<f64, String> {
        unsafe {
            let (mut create, mut exit, mut kernel, mut user) = (
                FileTime { low: 0, high: 0 }, FileTime { low: 0, high: 0 },
                FileTime { low: 0, high: 0 }, FileTime { low: 0, high: 0 });
            if GetProcessTimes(GetCurrentProcess(), &mut create, &mut exit, &mut kernel, &mut user) == 0 {
                return Err("GetProcessTimes failed".into());
            }
            let value = |time: FileTime| ((time.high as u64) << 32) | time.low as u64;
            Ok((value(kernel) + value(user)) as f64 / 10_000_000.0)
        }
    }
    pub fn peak_working_set() -> Result<Option<usize>, String> {
        unsafe {
            let mut counters: MemoryCounters = std::mem::zeroed();
            counters.cb = std::mem::size_of::<MemoryCounters>() as u32;
            if GetProcessMemoryInfo(GetCurrentProcess(), &mut counters, counters.cb) == 0 {
                return Err("GetProcessMemoryInfo failed".into());
            }
            Ok(Some(counters.peak))
        }
    }
    pub const CPU_CLOCK: &str = "GetProcessTimes kernel plus user";
}

#[cfg(unix)]
mod platform {
    pub fn cpu_seconds() -> Result<f64, String> {
        unsafe {
            let mut usage: libc::rusage = std::mem::zeroed();
            if libc::getrusage(libc::RUSAGE_SELF, &mut usage) != 0 {
                return Err("getrusage failed".into());
            }
            let seconds = |time: libc::timeval| time.tv_sec as f64 + time.tv_usec as f64 / 1_000_000.0;
            Ok(seconds(usage.ru_utime) + seconds(usage.ru_stime))
        }
    }
    pub fn peak_working_set() -> Result<Option<usize>, String> { Ok(None) }
    pub const CPU_CLOCK: &str = "getrusage RUSAGE_SELF user plus system";
}

#[derive(Default)]
struct Interval {
    iterations: usize,
    wall_polls: usize,
    cpu_polls: usize,
    wall: f64,
    cpu: f64,
}

fn measure<F>(duration: f64, mut work: F) -> Result<Interval, String>
where F: FnMut() -> Result<(), String> {
    let mut result = Interval::default();
    let cpu_start = platform::cpu_seconds()?;
    let started = Instant::now();
    let mut last_cpu_poll_wall = 0.0;
    loop {
        for _ in 0..8 {
            work()?;
            result.iterations += 1;
        }
        result.wall = started.elapsed().as_secs_f64();
        result.wall_polls += 1;
        if result.wall >= 60.0 {
            return Err("continuous interval exceeded 60-second budget".into());
        }
        if result.wall - last_cpu_poll_wall >= 0.25 {
            let current_cpu = platform::cpu_seconds()?;
            result.cpu_polls += 1;
            last_cpu_poll_wall = result.wall;
            if current_cpu >= cpu_start + duration && result.wall >= duration {
                break;
            }
        }
    }
    result.cpu = platform::cpu_seconds()? - cpu_start;
    result.wall = started.elapsed().as_secs_f64();
    Ok(result)
}

fn hash_bytes(bytes: &[u8]) -> u64 {
    let mut hash = 14_695_981_039_346_656_037_u64;
    for byte in bytes {
        hash ^= *byte as u64;
        hash = hash.wrapping_mul(1_099_511_628_211);
    }
    hash
}

fn projection(term: &mut Term<Listener>, listener: &Listener) -> Result<(u64, Value), String> {
    let value = trace::snapshot(term, listener, 0);
    let bytes = serde_json::to_vec(&value).map_err(|error| error.to_string())?;
    Ok((hash_bytes(&bytes), value))
}

fn correct(value: &Value, route: &str) -> bool {
    let screen = &value["screen"];
    if screen["row"] != 23 || screen["column"] != 0 || screen["wrap_pending"] != false {
        return false;
    }
    let cells = screen["cells"].as_array().unwrap();
    let history = screen["history"].as_array().unwrap();
    if cells.len() != 24 || history.len() != 100 {
        return false;
    }
    if route == "interactive" {
        for row in cells[..23].iter().chain(history.iter()) {
            for (column, cell) in row.as_array().unwrap().iter().enumerate() {
                let expected = if column < 8 { &"abcdeQgh"[column..column + 1] } else { "" };
                if cell["text"] != expected || cell["style"]["fg"] != -1 || cell["style"]["bg"] != -1 {
                    return false;
                }
            }
        }
        for cell in cells[23].as_array().unwrap() {
            if cell["text"] != "" || cell["style"]["fg"] != -1 || cell["style"]["bg"] != -1 {
                return false;
            }
        }
        return true;
    }
    for (row_index, row) in cells.iter().enumerate() {
        for (column, cell) in row.as_array().unwrap().iter().enumerate() {
            let expected = if row_index < 23 && column < 76 { "x" } else { "" };
            if cell["text"] != expected || cell["style"]["fg"] != -1 || cell["style"]["bg"] != -1 {
                return false;
            }
        }
    }
    for row in history {
        for (column, cell) in row.as_array().unwrap().iter().enumerate() {
            let expected = if column < 76 { "x" } else { "" };
            if cell["text"] != expected || cell["style"]["fg"] != -1 || cell["style"]["bg"] != -1 {
                return false;
            }
        }
    }
    true
}

fn duration_arg(value: &str) -> Result<f64, String> {
    let duration = value.parse::<f64>().map_err(|_| "invalid duration")?;
    if !duration.is_finite() || !(0.1..=10.0).contains(&duration) {
        return Err("invalid duration".into());
    }
    Ok(duration)
}

fn run() -> Result<(), String> {
    let args: Vec<String> = env::args().collect();
    if args.len() != 5 || (args[1] != "project" && args[1] != "interactive") {
        return Err("alacritty-measure project|interactive chunk observe(0|1) min_cpu_seconds".into());
    }
    let route = args[1].as_str();
    let chunk = args[2].parse::<usize>().map_err(|_| "invalid chunk")?;
    if chunk == 0 || chunk > 1_048_576 {
        return Err("invalid chunk".into());
    }
    let observe = match args[3].as_str() {
        "0" => false,
        "1" => true,
        _ => return Err("invalid observer".into()),
    };
    let duration = duration_arg(&args[4])?;
    let payload = if route == "interactive" {
        "abcdefgh\x1b[D\x1b[D\x1b[DZ\x08Q\r\n".repeat(1000)
    } else {
        format!("{}\r\n", "x".repeat(76)).repeat(1000)
    };
    let listener = Listener::default();
    let config = Config { scrolling_history: 100, ..Config::default() };
    let new_term = || Term::new(config.clone(), &Size { columns: 80, rows: 24 }, listener.clone());
    let oracle_started = Instant::now();
    let mut warmup = new_term();
    let mut parser: Processor = Processor::new();
    for offset in (0..payload.len()).step_by(chunk) {
        parser.advance(&mut warmup, &payload.as_bytes()[offset..(offset + chunk).min(payload.len())]);
    }
    let (expected_hash, expected_value) = projection(&mut warmup, &listener)?;
    if !correct(&expected_value, route) {
        return Err("warmup oracle failed".into());
    }
    drop(warmup);
    let preparation = oracle_started.elapsed().as_secs_f64();
    ALLOCATIONS.store(0, Ordering::Relaxed);
    ALLOCATED_BYTES.store(0, Ordering::Relaxed);
    OBSERVE.store(observe, Ordering::Relaxed);
    let mut term: Option<Term<Listener>> = None;
    let interval = measure(duration, || {
        term = Some(new_term());
        let mut parser: Processor = Processor::new();
        let current = term.as_mut().unwrap();
        for offset in (0..payload.len()).step_by(chunk) {
            parser.advance(current, &payload.as_bytes()[offset..(offset + chunk).min(payload.len())]);
        }
        if observe {
            let (actual, value) = projection(current, &listener)?;
            if actual != expected_hash || !correct(&value, route) {
                return Err("measured projection oracle failed".into());
            }
        }
        Ok(())
    })?;
    OBSERVE.store(false, Ordering::Relaxed);
    let checks = Instant::now();
    let (final_hash, final_value) = projection(term.as_mut().unwrap(), &listener)?;
    if final_hash != expected_hash || !correct(&final_value, route) {
        return Err("final state oracle failed".into());
    }
    let oracle_seconds = checks.elapsed().as_secs_f64();
    let allocations = ALLOCATIONS.load(Ordering::Relaxed);
    let allocated_bytes = ALLOCATED_BYTES.load(Ordering::Relaxed);
    let peak = platform::peak_working_set()?;
    let result = json!({
        "schema_version": 2,
        "kind": "core_cost",
        "valid": true,
        "route": route,
        "chunk": chunk,
        "observe": observe,
        "rounds": interval.iterations,
        "bytes_per_round": payload.len(),
        "cpu_floor_seconds": duration,
        "iterations": interval.iterations,
        "interval_wall_seconds": interval.wall,
        "interval_cpu_seconds": interval.cpu,
        "wall_gate_polls": interval.wall_polls,
        "cpu_gate_polls": interval.cpu_polls,
        "wall_gate_every_iterations": 8,
        "cpu_gate_min_wall_spacing_seconds": 0.25,
        "cpu_clock": platform::CPU_CLOCK,
        "wall_clock": "std::time::Instant",
        "warmup_and_oracle_seconds": preparation,
        "final_oracle_seconds": oracle_seconds,
        "oracle_scope": if observe { "warmup and final; every measured projection also hashed" } else { "warmup and final iteration" },
        "state_fnv1a64": expected_hash.to_string(),
        "ordinary_allocations": if observe { Some(allocations) } else { None },
        "ordinary_allocated_bytes": if observe { Some(allocated_bytes) } else { None },
        "allocation_scope": "Rust global allocator requests; not equivalent to C++ new or live memory",
        "peak_working_set_bytes": peak,
        "cpu_interval_differences": 1,
        "counted_parser_destructions": interval.iterations - 1,
        "counter_off_is_uninstrumented": false,
        "receive_seconds": Value::Null,
        "presented_done": Value::Null,
    });
    println!("{}", serde_json::to_string(&result).map_err(|error| error.to_string())?);
    Ok(())
}

fn main() {
    if let Err(error) = run() {
        OBSERVE.store(false, Ordering::Relaxed);
        eprintln!("alacritty measure failed: {error}");
        std::process::exit(2);
    }
}

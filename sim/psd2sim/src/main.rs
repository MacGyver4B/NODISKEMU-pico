//! psd2sim - petSD2+IEC-Firmware (RP2350B) in picoem am simlink-IEEE-Bus
//!
//!   psd2sim [-v] [-l vorausschau_us] firmware.bin bus
//!
//! Wie petsdsim (simavr) eine Kopplung mit Vorausschau: Eingaenge vom Bus
//! wirken L spaeter auf den RP2350, dafuer darf er bis Fortschritt der
//! anderen + L rechnen. Die 74FCT621A (Open Drain, nicht invertierend)
//! werden auf Pin-Ebene nachgebildet, Belegung laut Konzept (18.11.):
//!   16-23 D1-D8 (621 #1, Richtung GPIO 26: high = senden)
//!   27 ATN_I 28 IFC_I 29 REN_I 30 EOI_I 31 DAV_I 32 NRFD_I 33 NDAC_I
//!   34 EOI_O 35 DAV_O 36 NRFD_O 37 NDAC_O 38 SRQ_O
//! Ein Ausgang zieht die Leitung, wenn der Pin getrieben und low ist;
//! ungetrieben liegt er per Pull-up auf high (= Leitung frei).

use std::os::raw::{c_char, c_int};
use std::sync::atomic::Ordering;

use rp2350_emu::{Config, Emulator, EmulatorBuilder};

#[repr(C)]
struct SimlinkT {
    _p: [u8; 0],
}

const SIMLINK_BUS_IEEE: c_int = 0;
const SL_TYPE_PICOEM: u8 = 2;
const ATN: u8 = 0x01;
const DAV: u8 = 0x02;
const NRFD: u8 = 0x04;
const NDAC: u8 = 0x08;
const EOI: u8 = 0x10;
const SRQ: u8 = 0x20;
const IFC: u8 = 0x40;
const REN: u8 = 0x80;
const IEC_ATN: u8 = 0x01;
const IEC_CLK: u8 = 0x02;
const IEC_DATA: u8 = 0x04;
const IEC_SRQ: u8 = 0x08;
const IEC_RESET: u8 = 0x10;
const SIMLINK_BUS_IEC: c_int = 1;

extern "C" {
    fn simlink_open(name: *const c_char, bus: c_int, create: c_int, mask: u8) -> *mut SimlinkT;
    fn simlink_close(sl: *mut SimlinkT, unlink_bus: c_int);
    fn simlink_attach(sl: *mut SimlinkT, ty: u8, clockmaster: c_int) -> c_int;
    fn simlink_detach(sl: *mut SimlinkT, slot: c_int);
    fn simlink_set(sl: *mut SimlinkT, slot: c_int, t_ps: u64, pull: u8, data: u8);
    fn simlink_progress(sl: *mut SimlinkT, slot: c_int, t_ps: u64) -> c_int;
    fn simlink_start_time(sl: *mut SimlinkT, slot: c_int) -> u64;
    fn simlink_peers_progress(sl: *mut SimlinkT, slot: c_int, err: *mut c_int) -> u64;
    fn simlink_wait_peers(sl: *mut SimlinkT, slot: c_int, above: u64) -> u64;
    fn simlink_peek_others(sl: *mut SimlinkT, slot: c_int, t_ps: u64, data: *mut u8) -> u8;
    fn simlink_next_change(sl: *mut SimlinkT, slot: c_int, after: u64, upto: u64, t: *mut u64) -> c_int;
    fn simlink_others(sl: *mut SimlinkT, slot: c_int) -> c_int;
    fn sd_open(path: *const c_char, verbose: c_int) -> c_int;
    fn sd_blocks() -> std::os::raw::c_ulong;
    fn sd_select(ss: c_int);
    fn sd_xfer(b: u8) -> u8;
}

/// Postfach der Firmware (rp2350/emu.h): SD-Karte und Debugausgabe
const MBOX: u32 = 0x2008_0000;

fn mbox_byte(emu: &Emulator, i: u32) -> u8 {
    let w = emu.peek(MBOX + 16 + (i & !3));
    (w >> ((i & 3) * 8)) as u8
}

fn mbox(emu: &mut Emulator, line: &mut String) {
    let cmd = emu.peek(MBOX);
    if cmd == 0 {
        return;
    }
    let arg = emu.peek(MBOX + 4);
    let len = emu.peek(MBOX + 8).min(1024);
    match cmd {
        1 => unsafe { sd_select(arg as c_int) },
        2 => {
            let r = unsafe { sd_xfer(arg as u8) };
            emu.poke(MBOX + 12, r as u32);
        }
        3 => {
            for i in 0..len {
                let b = mbox_byte(emu, i);
                unsafe { sd_xfer(b) };
            }
        }
        4 => {
            let mut w = 0u32;
            for i in 0..len {
                let b = unsafe { sd_xfer(0xff) } as u32;
                w |= b << ((i & 3) * 8);
                if i & 3 == 3 || i + 1 == len {
                    emu.poke(MBOX + 16 + (i & !3), w);
                    w = 0;
                }
            }
        }
        5 => {
            for i in 0..len {
                let c = mbox_byte(emu, i) as char;
                if c == '\n' {
                    if std::env::var("PSD2_CYC").is_ok() {
                        eprintln!("petsd2 [{}]: {}", emu.cycles(), line.trim_end_matches('\r'));
                    } else {
                        eprintln!("petsd2: {}", line.trim_end_matches('\r'));
                    }
                    line.clear();
                } else {
                    line.push(c);
                }
            }
        }
        _ => eprintln!("psd2sim: unbekanntes Postfach-Kommando {}", cmd),
    }
    emu.poke(MBOX, 0);
}

/// Ein-/Ausgaben des RP2350 als 48-Bit-Masken (Wert, Ausgang aktiv)
fn pins_out(emu: &Emulator) -> (u64, u64) {
    let b = &emu.bus;
    let mut out_lo = b.sio.gpio_out & b.sio.gpio_oe;
    let mut oe_lo = b.sio.gpio_oe;
    let mut out_hi = b.sio.gpio_hi_out & b.sio.gpio_hi_oe;
    let mut oe_hi = b.sio.gpio_hi_oe;
    for pio in &b.pio {
        let (po_lo, po_hi) = pio.local_to_physical_pins(pio.pad_out);
        let (pe_lo, pe_hi) = pio.local_to_physical_pins(pio.pad_oe);
        out_lo = (out_lo & !pe_lo) | (po_lo & pe_lo);
        out_hi = (out_hi & !pe_hi) | (po_hi & pe_hi);
        oe_lo |= pe_lo;
        oe_hi |= pe_hi;
    }
    (
        (out_lo as u64) | ((out_hi as u64) << 32),
        (oe_lo as u64) | ((oe_hi as u64) << 32),
    )
}

fn pulled(out: u64, oe: u64, pin: u32) -> bool {
    (oe >> pin) & 1 == 1 && (out >> pin) & 1 == 0
}

fn high(out: u64, oe: u64, pin: u32) -> bool {
    (oe >> pin) & 1 == 1 && (out >> pin) & 1 == 1
}

/// Ein simlink-Bus der Bruecke
struct Bus {
    sl: *mut SimlinkT,
    slot: c_int,
    iec: bool,
    off: u64,               // Busstartzeit (Buszeit = Emulatorzeit + off)
    pub_pull: u8,
    pub_data: u8,
    pub_valid: bool,
    ext_pull: u8,           // andere Teilnehmer (bereits angewandt)
    ext_data: u8,
    last_ext: u64,          // Buszeit der letzten ausgewerteten Aenderung
    pend: std::collections::VecDeque<(u64, u8, u8)>,   // (Emulatorzeit, pull, data)
}

struct Bridge {
    verbose: u32,
    bus: Vec<Bus>,
}

impl Bridge {
    /// Buszustand (andere + eigene Leitungen) auf die Eingangspins
    fn apply(&mut self, emu: &mut Emulator, t: u64) {
        let (out, oe) = pins_out(emu);
        let mut val: u64 = 0;
        let mut mask: u64 = 0;
        {
            let mut put = |pin: u32, high_level: bool| {
                mask |= 1 << pin;
                if high_level {
                    val |= 1 << pin;
                }
            };
            for b in &self.bus {
                let l = b.ext_pull | b.pub_pull;
                let d = b.ext_data | b.pub_data;
                if !b.iec {
                    // 621: Eingang = Buspegel (high = frei)
                    put(27, l & ATN == 0);
                    put(28, l & IFC == 0);
                    put(29, l & REN == 0);
                    put(30, l & EOI == 0);
                    put(31, l & DAV == 0);
                    put(32, l & NRFD == 0);
                    put(33, l & NDAC == 0);
                    if !high(out, oe, 26) {
                        for i in 0..8 {
                            put(16 + i, d & (1 << i) == 0);
                        }
                    }
                } else {
                    // 74LVC14A invertiert: Pin high = Leitung low
                    put(39, l & IEC_CLK != 0);
                    put(40, l & IEC_DATA != 0);
                    put(41, l & IEC_ATN != 0);
                    put(42, l & IEC_SRQ != 0);
                }
            }
        }
        emu.bus.gpio_external_in.store(val as u32, Ordering::Relaxed);
        emu.bus.gpio_external_mask = mask as u32;
        emu.bus.set_gpio_external_in_hi((val >> 32) as u32, (mask >> 32) as u32);
        if self.verbose > 2 {
            eprintln!("psd2sim {:12.5} ms: Bus -> RP {:012x}", t as f64 / 1e9, val);
        }
    }

    /// Ausgangspins auf die Busse; true, wenn sich etwas geaendert hat
    fn publish(&mut self, emu: &mut Emulator, t: u64) -> bool {
        let (out, oe) = pins_out(emu);
        let mut changed = false;
        for b in self.bus.iter_mut() {
            let mut p = 0u8;
            let mut d = 0u8;
            if !b.iec {
                if pulled(out, oe, 34) { p |= EOI; }
                if pulled(out, oe, 35) { p |= DAV; }
                if pulled(out, oe, 36) { p |= NRFD; }
                if pulled(out, oe, 37) { p |= NDAC; }
                if pulled(out, oe, 38) { p |= SRQ; }
                if high(out, oe, 26) {
                    for i in 0..8 {
                        if pulled(out, oe, 16 + i) {
                            d |= 1 << i;
                        }
                    }
                }
            } else {
                // 74LS07: Pin low = Leitung low
                if pulled(out, oe, 43) { p |= IEC_CLK; }
                if pulled(out, oe, 44) { p |= IEC_DATA; }
                if pulled(out, oe, 45) { p |= IEC_ATN; }
                if pulled(out, oe, 46) { p |= IEC_SRQ; }
                if pulled(out, oe, 47) { p |= IEC_RESET; }
            }
            if !b.pub_valid || p != b.pub_pull || d != b.pub_data {
                b.pub_valid = true;
                b.pub_pull = p;
                b.pub_data = d;
                unsafe { simlink_set(b.sl, b.slot, t + b.off, p, d) };
                if self.verbose > 1 {
                    eprintln!("psd2sim {:12.5} ms: RP -> {} {:02x}/{:02x}", t as f64 / 1e9,
                              if b.iec { "IEC " } else { "IEEE" }, p, d);
                }
                changed = true;
            }
        }
        changed
    }
}


fn main() {
    let mut args: Vec<String> = std::env::args().skip(1).collect();
    let mut verbose = 0u32;
    let mut look: u64 = 2_000_000;
    let mut sdimg: Option<String> = None;
    let mut i = 0;
    while i < args.len() {
        match args[i].as_str() {
            "-v" => { verbose += 1; args.remove(i); }
            "-vv" => { verbose += 2; args.remove(i); }
            "-vvv" => { verbose += 3; args.remove(i); }
            "-s" => {
                sdimg = Some(args[i + 1].clone());
                args.drain(i..i + 2);
            }
            "-l" => {
                look = (args[i + 1].parse::<f64>().expect("-l us") * 1e6) as u64;
                args.drain(i..i + 2);
            }
            _ => i += 1,
        }
    }
    if args.len() != 2 && args.len() != 3 {
        eprintln!("usage: psd2sim [-v] [-l vorausschau_us] [-s sdkarte.img] firmware.bin ieeebus [iecbus]");
        std::process::exit(1);
    }
    let fw = std::fs::read(&args[0]).expect("Firmware");
    if let Some(p) = &sdimg {
        let c = std::ffi::CString::new(p.clone()).unwrap();
        if unsafe { sd_open(c.as_ptr(), verbose as c_int) } != 0 {
            eprintln!("psd2sim: SD-Abbild {} nicht lesbar", p);
            std::process::exit(1);
        }
        eprintln!("psd2sim: SD-Karte {} ({} Bloecke)", p, unsafe { sd_blocks() });
    }
    let mut uline = String::new();
    let bootrom = std::fs::read(std::env::var("PICOEM_BOOTROM").unwrap_or_else(|_| concat!(env!("CARGO_MANIFEST_DIR"), "/../picoem/roms/rp2350/bootrom-combined.bin").to_string())).expect("Bootrom");
    // Schrittweite in Takten: kleiner = Ausgaenge zeitgenauer am Bus
    // Standard: fein (8) nur, solange die Firmware die Interrupts sperrt
    // (AR6-Zeitfenster), sonst grob (64)
    let step: u32 = std::env::var("PSD2_STEP").ok().and_then(|v| v.parse().ok()).unwrap_or(8);
    let coarse: u64 = std::env::var("PSD2_COARSE").ok().and_then(|v| v.parse().ok()).unwrap_or(64);
    let mut emu = EmulatorBuilder::new(Config::default())
        .step_quantum(step)
        .build()
        .expect("Emulator");
    emu.load_bootrom(&bootrom);
    emu.load_flash(&fw);
    emu.reset();
    // Bootrom-Pruefung des Flash-Abbilds umgehen (wie die OneROM-Werkzeuge
    // in picoem): Kern 0 startet direkt ueber die Vektortabelle am Anfang
    // des Flash; Kern 1 bleibt im Bootrom und wartet auf multicore_launch.
    {
        let sp = u32::from_le_bytes(fw[0..4].try_into().unwrap());
        let pc = u32::from_le_bytes(fw[4..8].try_into().unwrap()) & !1;
        emu.core_mut(0).regs.set_sp(sp);
        emu.core_mut(0).regs.set_pc(pc);
        // wie der Bootrom: Vektortabelle im Flash (das SDK kopiert sie ins RAM)
        emu.core_mut(0).ppb.vtor = 0x1000_0000;
        // nach dem Reset steht FAULTMASK in picoem auf 1, der Bootrom gibt
        // die Ausnahmen vor dem Sprung frei
        emu.core_mut(0).regs.faultmask = 0;
    }
    if let Ok(ms) = std::env::var("PSD2_SOLO") {
        // ohne Bus: alle Eingaenge frei, nur Firmware und Postfach
        let ms: f64 = ms.parse().unwrap_or(1000.0);
        let mask: u64 = (0xff << 16) | (0x7f << 27);
        emu.bus.gpio_external_in.store(mask as u32, Ordering::Relaxed);
        emu.bus.gpio_external_mask = mask as u32;
        emu.bus.set_gpio_external_in_hi((mask >> 32) as u32, (mask >> 32) as u32);
        let wall = std::time::Instant::now();
        let hz = emu.bus.sys_clk_hz().max(1) as f64;
        let atn_at: f64 = std::env::var("PSD2_SOLO_ATN").ok().and_then(|v| v.parse().ok()).unwrap_or(-1.0);
        let mut atn_done = false;
        let mut rep = 0;
        while (emu.cycles() as f64) / hz * 1000.0 < ms {
            emu.run(64).expect("run");
            mbox(&mut emu, &mut uline);
            let now = (emu.cycles() as f64) / hz * 1000.0;
            if atn_at >= 0.0 && !atn_done && now >= atn_at {
                let v = mask & !(1u64 << 27);
                emu.bus.gpio_external_in.store(v as u32, Ordering::Relaxed);
                atn_done = true;
                eprintln!("psd2sim: ATN low bei {:.3} ms", now);
            }
            if atn_done && rep < 20 {
                rep += 1;
                let (out, oe) = pins_out(&emu);
                let p = &emu.bus.pio[0];
                let pp = &emu.core(0).ppb;
                eprintln!("  iser {:08x} {:08x} primask {} basepri {} faultmask {} ipsr {} icsr {:08x}", pp.nvic_iser[0].load(Ordering::Relaxed),
                    pp.nvic_iser[1].load(Ordering::Relaxed), emu.core(0).regs.primask, emu.core(0).regs.basepri, emu.core(0).regs.faultmask, emu.core(0).regs.ipsr(), pp.icsr);
                eprintln!("  {:.4} ms PC {:08x} raw {:05x} inte {:05x} ints {:05x} NRFD_O pulled {} NDAC_O pulled {} in {:012x}",
                    now, emu.core(0).regs.pc(), p.raw_intr_rp2350(), p.int0_inte, p.int0_ints_rp2350(),
                    pulled(out, oe, 36), pulled(out, oe, 37), emu.gpio_read_all());
            }
        }
        {
            let sp = emu.core(0).regs.sp();
            eprintln!("psd2sim: SP {:08x} gestapelt LR {:08x} PC {:08x} xPSR {:08x}", sp,
                emu.peek(sp + 20), emu.peek(sp + 24), emu.peek(sp + 28));
        }
        eprintln!("psd2sim: {} ms simuliert in {:.1} s, PC {:08x}", ms,
                  wall.elapsed().as_secs_f64(), emu.core(0).regs.pc());
        return;
    }
    if let Ok(n) = std::env::var("PSD2_TRACE") {
        let n: u64 = n.parse().unwrap_or(2000);
        let mut last = 0u32;
        for k in 0..n {
            emu.run(1).expect("run");
            let pc = emu.core(0).regs.pc();
            if pc.wrapping_sub(last) > 8 {
                eprintln!("trace {:6} cyc {:8} pc {:08x}", k, emu.cycles(), pc);
            }
            last = pc;
        }
        return;
    }

    let mut br = Bridge { verbose, bus: Vec::new() };
    for (k, name) in args[1..].iter().enumerate() {
        let iec = k == 1;
        let kind = if iec { SIMLINK_BUS_IEC } else { SIMLINK_BUS_IEEE };
        let cn = std::ffi::CString::new(name.clone()).unwrap();
        let sl = unsafe {
            let s = simlink_open(cn.as_ptr(), kind, 0, 0xff);
            if s.is_null() { simlink_open(cn.as_ptr(), kind, 1, 0xff) } else { s }
        };
        if sl.is_null() {
            eprintln!("psd2sim: simlink_open {} gescheitert", name);
            std::process::exit(1);
        }
        let slot = unsafe { simlink_attach(sl, SL_TYPE_PICOEM, 0) };
        if slot < 0 {
            eprintln!("psd2sim: kein freier Platz an {}", name);
            std::process::exit(1);
        }
        br.bus.push(Bus {
            sl, slot, iec, off: 0, pub_pull: 0, pub_data: 0, pub_valid: false,
            ext_pull: 0, ext_data: 0, last_ext: 0, pend: Default::default(),
        });
    }
    eprintln!("psd2sim: {} ({} Bytes) an {}", args[0], fw.len(), args[1..].join(" + "));
    let mut waited = 0;
    while br.bus.iter().any(|b| unsafe { simlink_others(b.sl, b.slot) } == 0) {
        std::thread::sleep(std::time::Duration::from_millis(10));
        waited += 1;
        if waited > 3000 {
            eprintln!("psd2sim: nach 30 s keine Gegenseite");
            std::process::exit(1);
        }
    }
    let mut t: u64 = 0;            // Emulatorzeit in ps
    let mut last_cyc = emu.cycles();
    for b in br.bus.iter_mut() {
        b.off = unsafe { simlink_start_time(b.sl, b.slot) };
        b.last_ext = b.off;
        let mut d0 = 0u8;
        b.ext_pull = unsafe { simlink_peek_others(b.sl, b.slot, b.off, &mut d0) };
        b.ext_data = d0;
    }
    br.apply(&mut emu, t);
    br.publish(&mut emu, t);
    for b in &br.bus {
        unsafe { simlink_progress(b.sl, b.slot, b.off + 1) };
    }
    let mut done: u64 = 0;
    let mut next_rep: u64 = 0;
    let wall = std::time::Instant::now();

    'main: loop {
        // wie weit duerfen wir? je Bus: Fortschritt der anderen - 2 (VICE
        // stempelt spaete Aenderungen noch mit done) + Vorausschau
        let mut target = u64::MAX;
        for b in br.bus.iter_mut() {
            let mut err: c_int = 0;
            let mut m = unsafe { simlink_peers_progress(b.sl, b.slot, &mut err) };
            if err != 0 || unsafe { simlink_others(b.sl, b.slot) } == 0 {
                eprintln!("psd2sim: Gegenseite beendet");
                break 'main;
            }
            if m == u64::MAX {
                continue;   // Gegenseite gibt keine Grenze vor
            }
            if (m.saturating_sub(2) + look).saturating_sub(b.off) <= done {
                m = unsafe { simlink_wait_peers(b.sl, b.slot, (done + b.off + 1).saturating_sub(look)) };
                if unsafe { simlink_others(b.sl, b.slot) } == 0 {
                    eprintln!("psd2sim: Gegenseite beendet");
                    break 'main;
                }
                if m == u64::MAX {
                    continue;
                }
            }
            let upto = m - 2;
            let mut tc: u64 = 0;
            while unsafe { simlink_next_change(b.sl, b.slot, b.last_ext, upto, &mut tc) } != 0 {
                let mut d = 0u8;
                let l = unsafe { simlink_peek_others(b.sl, b.slot, tc, &mut d) };
                b.pend.push_back((tc - b.off + look, l, d));
                b.last_ext = tc;
            }
            {
                let mut d = 0u8;
                let l = unsafe { simlink_peek_others(b.sl, b.slot, upto, &mut d) };
                let (ql, qd) = b.pend.back().map(|e| (e.1, e.2)).unwrap_or((b.ext_pull, b.ext_data));
                if l != ql || d != qd {
                    b.pend.push_back((upto - b.off + look, l, d));
                }
                if upto > b.last_ext {
                    b.last_ext = upto;
                }
            }
            let tg = (upto + look).saturating_sub(b.off);
            if tg < target {
                target = tg;
            }
        }
        if target == u64::MAX || target <= t {
            std::thread::sleep(std::time::Duration::from_micros(200));
            continue;
        }
        while t < target {
            let mut chg = false;
            for b in br.bus.iter_mut() {
                while let Some(&(at, l, d)) = b.pend.front() {
                    if at > t {
                        break;
                    }
                    b.pend.pop_front();
                    if l != b.ext_pull || d != b.ext_data {
                        b.ext_pull = l;
                        b.ext_data = d;
                        chg = true;
                    }
                }
            }
            if chg {
                br.apply(&mut emu, t);
            }
            let n = if emu.core(0).regs.primask != 0 { step as u64 } else { coarse };
            emu.run(n).expect("run");
            mbox(&mut emu, &mut uline);
            let c = emu.cycles();
            let hz = emu.bus.sys_clk_hz().max(1) as u64;
            t += (c - last_cyc) * 1_000_000_000_000 / hz;
            last_cyc = c;
            if br.publish(&mut emu, t) {
                // eigene Ausgaenge wirken sofort auf die eigenen Eingaenge
                br.apply(&mut emu, t);
            }
        }
        done = t;
        for b in &br.bus {
            unsafe { simlink_progress(b.sl, b.slot, done + b.off + 1) };
        }
        if verbose > 0 && t >= next_rep {
            eprintln!(
                "psd2sim {:10.3} ms (Echtzeit {:.1} s): PC {:08x}, in {:012x}",
                t as f64 / 1e9, wall.elapsed().as_secs_f64(), emu.core(0).regs.pc(), emu.gpio_read_all()
            );
            next_rep = t + 250_000_000_000;
        }
    }
    for b in &br.bus {
        unsafe {
            simlink_detach(b.sl, b.slot);
            simlink_close(b.sl, 0);
        }
    }
}

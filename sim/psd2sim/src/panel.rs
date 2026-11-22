//! Bedienfeld des petSD+ (Board petsd2_dip40) fuer psd2sim:
//!   - LCD 20x4 (HD44780U ROM-Code A00, mit PSD2_LCDROM=A02 der europaeische / WS0010) an GPIO 16/17/18/2 (DB4-DB7), 13 (RS), 14 (E)
//!   - Tasten PREV/NEXT/SEL als Widerstandsleiter an ADC0 (GPIO 40) hinter
//!     dem Teiler 56k/100k
//!   - I2C1: ATtiny25 (0x26, PWM Kontrast/Helligkeit, IO_IEC) und DS3231 (0x68)
//!   - Webseite auf localhost (Anzeige und Tasten), Tasten auch per HTTP
//!     skriptbar: curl -X POST 'http://localhost:8488/key?k=SEL&ms=200'

use std::io::{BufRead, BufReader, Read, Write};
use std::net::TcpListener;
use std::sync::{Arc, Mutex};
use std::time::{Duration, SystemTime, UNIX_EPOCH};

use rp2350_emu::peripherals::i2c::I2cTarget;
use rp2350_emu::Emulator;

pub const KEY_SEL: u8 = 1;
pub const KEY_NEXT: u8 = 2;
pub const KEY_PREV: u8 = 4;

// Belegung wie pico/boards/petsd2_dip40.h
const LCD_DB: [u32; 4] = [16, 17, 18, 2];
const LCD_RS: u32 = 13;
const LCD_E: u32 = 14;
const ADC_CH: u32 = 0;      // GPIO 40

/// HD44780 im 4-Bit-Betrieb, nur Schreiben
#[derive(Clone)]
pub struct Lcd {
    pub ddram: [u8; 128],
    pub cgram: [u8; 64],
    four_bit: bool,
    high: Option<u8>,
    addr: u8,
    to_cg: bool,
    inc: bool,
    pub on: bool,
    pub cursor: bool,
    pub writes: u64,
    /// ROM-Code des Zeichensatzes: false = A00 (Vorgabe), true = A02
    pub a02: bool,
}

impl Lcd {
    fn new() -> Self {
        Lcd {
            ddram: [b' '; 128], cgram: [0; 64], four_bit: false, high: None, addr: 0,
            to_cg: false, inc: true, on: false, cursor: false, writes: 0,
            a02: std::env::var("PSD2_LCDROM").map(|v| v.eq_ignore_ascii_case("A02")).unwrap_or(false),
        }
    }

    /// fallende Flanke an E mit RS und DB4-DB7
    fn strobe(&mut self, rs: bool, nib: u8) {
        if !self.four_bit {
            // 8-Bit-Betrieb: DB0-DB3 liegen nicht an (gelten als 0)
            self.byte(rs, nib << 4);
            return;
        }
        match self.high.take() {
            None => self.high = Some(nib),
            Some(h) => self.byte(rs, (h << 4) | nib),
        }
    }

    fn byte(&mut self, rs: bool, v: u8) {
        self.writes += 1;
        if rs {
            if self.to_cg {
                self.cgram[(self.addr & 63) as usize] = v;
                self.addr = (self.addr.wrapping_add(1)) & 63;
            } else {
                self.ddram[(self.addr & 127) as usize] = v;
                self.addr = if self.inc { self.addr.wrapping_add(1) } else { self.addr.wrapping_sub(1) } & 127;
            }
            return;
        }
        if v & 0x80 != 0 {
            self.addr = v & 0x7f;
            self.to_cg = false;
        } else if v & 0x40 != 0 {
            self.addr = v & 0x3f;
            self.to_cg = true;
        } else if v & 0x20 != 0 {
            let four = v & 0x10 == 0;
            if four != self.four_bit {
                self.four_bit = four;
                self.high = None;
            }
        } else if v & 0x10 != 0 {
            // Cursor/Anzeige schieben: nicht nachgebildet
        } else if v & 0x08 != 0 {
            self.on = v & 0x04 != 0;
            self.cursor = v & 0x03 != 0;
        } else if v & 0x04 != 0 {
            self.inc = v & 0x02 != 0;
        } else if v & 0x02 != 0 {
            self.addr = 0;
            self.to_cg = false;
        } else if v & 0x01 != 0 {
            self.ddram = [b' '; 128];
            self.addr = 0;
            self.to_cg = false;
            self.inc = true;
        }
    }

    /// Zeile 0-3 eines 20x4-Moduls
    pub fn line(&self, y: usize) -> [u8; 20] {
        let base = [0x00usize, 0x40, 0x14, 0x54][y];
        let mut l = [0u8; 20];
        l.copy_from_slice(&self.ddram[base..base + 20]);
        l
    }

    /// Cursorposition (Zeile, Spalte), wenn der Cursor an ist
    pub fn cursor_pos(&self) -> Option<(usize, usize)> {
        if !self.cursor || self.to_cg {
            return None;
        }
        let a = self.addr as usize;
        [0x00usize, 0x40, 0x14, 0x54].iter().position(|&b| a >= b && a < b + 20)
            .map(|y| (y, a - [0x00usize, 0x40, 0x14, 0x54][y]))
    }

    /// Text plus Cursorzeile (fuer /text und PSD2_LCDDUMP)
    pub fn text_with_cursor(&self) -> String {
        let mut t = self.text();
        if let Some((y, x)) = self.cursor_pos() {
            t.push_str(&format!("\nCursor: Zeile {}, Spalte {}", y + 1, x + 1));
        }
        t
    }

    pub fn text(&self) -> String {
        (0..4).map(|y| {
            self.line(y).iter().map(|&c| match c {
                0..=15 => '\u{2592}',
                // A00: Yen statt Backslash, Pfeile statt Tilde/DEL, oben Katakana
                0x5c if !self.a02 => '\u{a5}',
                0x7e if !self.a02 => '\u{2192}',
                0x7f if !self.a02 => '\u{2190}',
                0xff if !self.a02 => '\u{2588}',
                // A02: unten ASCII (0x7F = Haus), oben weitgehend Latin-1
                0x20..=0x7e => c as char,
                0x7f => '\u{2302}',
                0xa0..=0xff if self.a02 => c as char,
                _ => '?',
            }).collect::<String>()
        }).collect::<Vec<_>>().join("\n")
    }
}

/// ATtiny25 des petSD+ 2.x (lcd-i2c-pwm-io)
pub struct Attiny {
    pub contrast: u8,
    pub brightness: u8,   // PWM-Wert (255 - Helligkeit)
    pub io_iec: u8,
}

/// DS3231 mit Zeit vom Rechner plus Versatz aus Schreibzugriffen
pub struct Ds3231 {
    offset: i64,          // Sekunden zur Rechnerzeit
    regs: [u8; 19],       // 7.. (Alarme, Steuerung, Status, ...)
}

fn bcd(v: u32) -> u8 { (((v / 10) << 4) | (v % 10)) as u8 }
fn unbcd(v: u8) -> u32 { ((v >> 4) * 10 + (v & 15)) as u32 }

/// Tage seit 1970-01-01 -> (Jahr, Monat, Tag); Howard Hinnant
fn civil(days: i64) -> (i64, u32, u32) {
    let z = days + 719468;
    let era = z.div_euclid(146097);
    let doe = z - era * 146097;
    let yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    let y = yoe + era * 400;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let d = (doy - (153 * mp + 2) / 5 + 1) as u32;
    let m = if mp < 10 { mp + 3 } else { mp - 9 } as u32;
    (if m <= 2 { y + 1 } else { y }, m, d)
}

fn days_from_civil(y: i64, m: u32, d: u32) -> i64 {
    let y = if m <= 2 { y - 1 } else { y };
    let era = y.div_euclid(400);
    let yoe = y - era * 400;
    let mp = (m as i64 + 9) % 12;
    let doy = (153 * mp + 2) / 5 + d as i64 - 1;
    let doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    era * 146097 + doe - 719468
}

impl Ds3231 {
    fn now(&self) -> i64 {
        // Ortszeit wie ein gestelltes Geraet: TZ-Versatz aus PSD2_TZ (Stunden)
        let tz: i64 = std::env::var("PSD2_TZ").ok().and_then(|v| v.parse().ok()).unwrap_or(1);
        SystemTime::now().duration_since(UNIX_EPOCH).map(|d| d.as_secs() as i64).unwrap_or(0)
            + tz * 3600 + self.offset
    }

    fn time_regs(&self) -> [u8; 7] {
        let t = self.now();
        let days = t.div_euclid(86400);
        let s = t.rem_euclid(86400) as u32;
        let (y, m, d) = civil(days);
        let dow = ((days + 4).rem_euclid(7) + 1) as u32;   // 1 = Sonntag
        [bcd(s % 60), bcd(s / 60 % 60), bcd(s / 3600), bcd(dow), bcd(d), bcd(m), bcd((y % 100) as u32)]
    }

    fn set_time(&mut self, r: &[u8; 7]) {
        let days = days_from_civil(2000 + unbcd(r[6]) as i64, unbcd(r[5] & 0x1f), unbcd(r[4]));
        let t = days * 86400 + (unbcd(r[2] & 0x3f) * 3600 + unbcd(r[1]) * 60 + unbcd(r[0] & 0x7f)) as i64;
        self.offset += t - self.now();
    }
}

/// Gemeinsamer Zustand fuer Emulator-Schleife und Webseite
pub struct Shared {
    pub lcd: Lcd,
    pub tiny: Attiny,
    pub rtc: Ds3231,
    pub keys: u8,
    pub key_until: Option<(f64, u8)>,   // (simulierte ms, Tasten)
    pub sim_ms: f64,
}

pub type Panel = Arc<Mutex<Shared>>;

pub fn new_panel() -> Panel {
    let mut regs = [0u8; 19];
    regs[14] = 0x1c;      // CONTROL nach dem Einschalten
    Arc::new(Mutex::new(Shared {
        lcd: Lcd::new(),
        tiny: Attiny { contrast: 2, brightness: 0, io_iec: 1 },
        rtc: Ds3231 { offset: 0, regs },
        keys: 0,
        key_until: None,
        sim_ms: 0.0,
    }))
}

/// I2C1 mit ATtiny25 und DS3231
struct I2cBus {
    p: Panel,
    dev: u8,
    first: bool,
    ptr: u8,
    tbuf: Option<[u8; 7]>,
    verbose: u32,
}

impl I2cTarget for I2cBus {
    fn start(&mut self, addr: u8, read: bool) -> bool {
        if self.verbose > 0 {
            eprintln!("psd2sim: I2C Start {:02x} {}", addr, if read { "lesen" } else { "schreiben" });
        }
        self.dev = addr;
        self.first = true;
        if addr == 0x68 && self.tbuf.is_none() {
            // Zeit beim Zugriff einfrieren, wie der Puffer im DS3231
            self.tbuf = Some(self.p.lock().unwrap().rtc.time_regs());
        }
        addr == 0x26 || addr == 0x68
    }

    fn write(&mut self, byte: u8) -> bool {
        if self.first {
            self.ptr = byte;
            self.first = false;
            return true;
        }
        let mut s = self.p.lock().unwrap();
        match self.dev {
            0x26 => {
                match self.ptr {
                    1 => s.tiny.contrast = byte,
                    2 => s.tiny.brightness = byte,
                    3 => {
                        if self.verbose > 0 || s.tiny.io_iec != byte {
                            eprintln!("psd2sim: ATtiny25 IO_IEC = {}", byte);
                        }
                        s.tiny.io_iec = byte;
                    }
                    _ => {}
                }
            }
            _ => {
                let r = self.ptr as usize;
                if r < 7 {
                    let t = self.tbuf.get_or_insert_with(|| s.rtc.time_regs());
                    t[r] = byte;
                } else if r < 0x11 {
                    s.rtc.regs[r] = byte;      // 0x11/0x12 (Temperatur) nur lesbar
                }
            }
        }
        self.ptr = self.ptr.wrapping_add(1);
        true
    }

    fn read(&mut self) -> u8 {
        let s = self.p.lock().unwrap();
        let v = match self.dev {
            0x26 => 1,     // I2C_SOFTWARE_VERSION, alte Firmware liefert sie immer
            _ => {
                let r = self.ptr as usize;
                if r < 7 {
                    self.tbuf.unwrap_or_else(|| s.rtc.time_regs())[r]
                } else if r == 0x11 {
                    21     // 21 Grad
                } else if r < 0x13 {
                    s.rtc.regs[r]
                } else {
                    0
                }
            }
        };
        self.ptr = self.ptr.wrapping_add(1);
        v
    }

    fn stop(&mut self) {
        if self.dev == 0x68 {
            if let Some(t) = self.tbuf.take() {
                let mut s = self.p.lock().unwrap();
                if t != s.rtc.time_regs() {
                    s.rtc.set_time(&t);
                }
            }
        }
    }
}

/// ADC-Rohwert (12 Bit hinter dem Teiler) fuer eine Tastenkombination
pub fn key_raw(keys: u8) -> u16 {
    // Mitten der Fenster auf der 10-Bit-Skala des AVR (5 V)
    const C: [u32; 8] = [572, 776, 652, 931, 606, 839, 697, 1023];
    let c = C[(keys & 7) as usize];
    // Pinspannung c/1024*5 V, Teiler 100/156, ADC 4096 bei 3,3 V
    let raw = (c as f64 / 1024.0 * 5.0 * 100.0 / 156.0 / 3.3 * 4096.0).round();
    raw.min(4095.0) as u16
}

/// Leitet Pins und I2C des Emulators auf das Bedienfeld
pub struct PanelLink {
    pub p: Panel,
    last_e: bool,
    keys: u8,
    pub dump: bool,
    last_text: String,
}

impl PanelLink {
    pub fn attach(emu: &mut Emulator, p: Panel, verbose: u32) -> Self {
        emu.i2c_attach_target(1, Box::new(I2cBus {
            p: p.clone(), dev: 0, first: true, ptr: 0, tbuf: None, verbose,
        }));
        let keys = p.lock().unwrap().keys;
        emu.adc_set_input(ADC_CH, Some(key_raw(keys)));
        PanelLink { p, last_e: false, keys, dump: std::env::var("PSD2_LCDDUMP").is_ok(), last_text: String::new() }
    }

    /// nach jedem Emulatorschritt; out/oe: Ausgangspins (pins_out)
    pub fn poll(&mut self, out: u64, oe: u64) {
        let e = (oe >> LCD_E) & 1 == 1 && (out >> LCD_E) & 1 == 1;
        if self.last_e && !e {
            let rs = (out >> LCD_RS) & 1 == 1;
            let mut nib = 0u8;
            for (i, &pin) in LCD_DB.iter().enumerate() {
                if (out >> pin) & 1 == 1 { nib |= 1 << i; }
            }
            self.p.lock().unwrap().lcd.strobe(rs, nib);
        }
        self.last_e = e;
    }

    /// seltener aufrufen (z. B. jede simulierte Millisekunde)
    pub fn tick(&mut self, emu: &mut Emulator, sim_ms: f64) {
        let mut s = self.p.lock().unwrap();
        s.sim_ms = sim_ms;
        if let Some((until, k)) = s.key_until {
            if sim_ms >= until {
                s.keys &= !k;
                s.key_until = None;
            }
        }
        if s.keys != self.keys {
            self.keys = s.keys;
            emu.adc_set_input(ADC_CH, Some(key_raw(self.keys)));
            eprintln!("psd2sim: Tasten {}{}{} bei {:.0} ms",
                if self.keys & KEY_PREV != 0 { "PREV " } else { "" },
                if self.keys & KEY_NEXT != 0 { "NEXT " } else { "" },
                if self.keys & KEY_SEL != 0 { "SEL" } else { if self.keys == 0 { "los" } else { "" } },
                sim_ms);
        }
        if self.dump {
            let t = s.lcd.text();
            let c = s.lcd.cursor_pos();
            let key = format!("{}{:?}", t, c);
            if key != self.last_text {
                eprintln!("psd2sim: LCD bei {:.0} ms{}\n+--------------------+\n|{}|\n+--------------------+",
                          sim_ms, c.map(|(y, x)| format!(", Cursor Zeile {} Spalte {}", y + 1, x + 1)).unwrap_or_default(),
                          t.replace('\n', "|\n|"));
                self.last_text = key;
            }
        }
    }

    pub fn io_iec(&self) -> bool {
        self.p.lock().unwrap().tiny.io_iec != 0
    }
}

fn key_mask(name: &str) -> u8 {
    let mut m = 0;
    for k in name.split(|c| c == ',' || c == '+') {
        m |= match k.to_ascii_uppercase().as_str() {
            "PREV" => KEY_PREV,
            "NEXT" => KEY_NEXT,
            "SEL" | "SELECT" => KEY_SEL,
            _ => 0,
        };
    }
    m
}

/// Tasten aus dem Startparameter (z. B. "PREV" fuer die Diagnose)
pub fn hold_keys(p: &Panel, names: &str) {
    p.lock().unwrap().keys = key_mask(names);
}

fn json_escape(s: &str) -> String {
    s.chars().map(|c| match c {
        '"' => "\\\"".to_string(),
        '\\' => "\\\\".to_string(),
        '\n' => "\\n".to_string(),
        c if (c as u32) < 0x20 => format!("\\u{:04x}", c as u32),
        c => c.to_string(),
    }).collect()
}

fn state_json(s: &Shared) -> String {
    let mut lines = Vec::new();
    for y in 0..4 {
        let l = s.lcd.line(y);
        lines.push(format!("[{}]", l.iter().map(|c| c.to_string()).collect::<Vec<_>>().join(",")));
    }
    format!(
        "{{\"rom\":\"{}\",\"lines\":[{}],\"cgram\":[{}],\"cursor\":{},\"on\":{},\"keys\":{},\"contrast\":{},\"brightness\":{},\"io_iec\":{},\"ms\":{:.0},\"text\":\"{}\"}}",
        if s.lcd.a02 { "A02" } else { "A00" },
        lines.join(","),
        s.lcd.cgram.iter().map(|c| c.to_string()).collect::<Vec<_>>().join(","),
        s.lcd.cursor_pos().map(|(y, x)| format!("[{},{}]", y, x)).unwrap_or_else(|| "null".into()),
        s.lcd.on, s.keys, s.tiny.contrast, 255 - s.tiny.brightness as u32, s.tiny.io_iec, s.sim_ms,
        json_escape(&s.lcd.text())
    )
}

const PAGE: &str = include_str!("panel.html");

fn respond(stream: &mut std::net::TcpStream, ctype: &str, body: &str) {
    let _ = write!(stream, "HTTP/1.1 200 OK\r\nContent-Type: {}\r\nContent-Length: {}\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n{}",
                   ctype, body.len(), body);
}

fn query(path: &str, key: &str) -> Option<String> {
    let q = path.split_once('?')?.1;
    q.split('&').find_map(|kv| {
        let (k, v) = kv.split_once('=')?;
        (k == key).then(|| v.to_string())
    })
}

/// Webseite auf 127.0.0.1:port (eigener Thread)
pub fn serve(p: Panel, port: u16) {
    let l = match TcpListener::bind(("127.0.0.1", port)) {
        Ok(l) => l,
        Err(e) => {
            eprintln!("psd2sim: Webseite auf Port {} nicht moeglich: {}", port, e);
            return;
        }
    };
    eprintln!("psd2sim: Bedienfeld unter http://localhost:{}/", port);
    std::thread::spawn(move || {
        for s in l.incoming() {
            let Ok(mut s) = s else { continue };
            let _ = s.set_read_timeout(Some(Duration::from_secs(2)));
            let mut r = BufReader::new(match s.try_clone() { Ok(c) => c, Err(_) => continue });
            let mut req = String::new();
            if r.read_line(&mut req).is_err() { continue; }
            let mut len = 0usize;
            loop {
                let mut h = String::new();
                if r.read_line(&mut h).is_err() || h.trim().is_empty() { break; }
                if let Some(v) = h.to_ascii_lowercase().strip_prefix("content-length:") {
                    len = v.trim().parse().unwrap_or(0);
                }
            }
            let mut body = vec![0u8; len.min(4096)];
            let _ = r.read_exact(&mut body);
            let mut it = req.split_whitespace();
            let method = it.next().unwrap_or("");
            let path = it.next().unwrap_or("/").to_string();
            if path == "/" || path.starts_with("/index") {
                respond(&mut s, "text/html; charset=utf-8", PAGE);
            } else if path.starts_with("/state") {
                let j = state_json(&p.lock().unwrap());
                respond(&mut s, "application/json", &j);
            } else if path.starts_with("/text") {
                let t = p.lock().unwrap().lcd.text_with_cursor();
                respond(&mut s, "text/plain; charset=utf-8", &(t + "\n"));
            } else if path.starts_with("/key") && method == "POST" {
                // /key?k=SEL&ms=200 (antippen), /key?k=SEL&down=1 / &down=0 (halten/loslassen)
                let k = key_mask(&query(&path, "k").unwrap_or_default());
                let mut sh = p.lock().unwrap();
                match query(&path, "down").as_deref() {
                    Some("1") => { sh.keys |= k; sh.key_until = None; }
                    Some("0") => { sh.keys &= !k; }
                    _ => {
                        let ms: u64 = query(&path, "ms").and_then(|v| v.parse().ok()).unwrap_or(250);
                        sh.keys |= k;
                        let until = sh.sim_ms + ms as f64;     // simulierte Zeit
                        sh.key_until = Some((until, k));
                    }
                }
                drop(sh);
                respond(&mut s, "text/plain", "ok\n");
            } else {
                let _ = write!(s, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            }
        }
    });
}

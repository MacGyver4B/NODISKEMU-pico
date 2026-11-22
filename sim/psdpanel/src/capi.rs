//! C-Schnittstelle fuer petsdsim (simavr): ein Bedienfeld je Prozess

use std::ffi::{c_char, CStr};
use std::sync::Mutex;

use crate::{hold_keys, new_panel, serve, I2cDev, Link};

struct Ctx {
    link: Link,
    i2c: I2cDev,
}

static CTX: Mutex<Option<Ctx>> = Mutex::new(None);

fn with<R>(f: impl FnOnce(&mut Ctx) -> R, dflt: R) -> R {
    match CTX.lock().unwrap().as_mut() {
        Some(c) => f(c),
        None => dflt,
    }
}

/// Bedienfeld anlegen; port 0 = ohne Webseite; hold: Tasten ab Start
/// ("PREV", "NEXT+SEL") oder NULL
#[no_mangle]
pub extern "C" fn psdpanel_init(port: u16, hold: *const c_char, verbose: u32) {
    let p = new_panel();
    if !hold.is_null() {
        let h = unsafe { CStr::from_ptr(hold) }.to_string_lossy().into_owned();
        hold_keys(&p, &h);
    }
    if port != 0 {
        serve(p.clone(), port);
    }
    *CTX.lock().unwrap() = Some(Ctx { link: Link::new(p.clone()), i2c: I2cDev::new(p, verbose) });
}

/// Pegel an E, RS und DB4-DB7 (Bit 0 = DB4)
#[no_mangle]
pub extern "C" fn psdpanel_lcd_pins(e: u8, rs: u8, nib: u8) {
    with(|c| c.link.lcd_pins(e != 0, rs != 0, nib), ())
}

/// I2C-Start mit 7-Bit-Adresse; 1 = ACK
#[no_mangle]
pub extern "C" fn psdpanel_i2c_start(addr: u8, read: u8) -> u8 {
    with(|c| c.i2c.start(addr, read != 0) as u8, 0)
}

#[no_mangle]
pub extern "C" fn psdpanel_i2c_write(byte: u8) -> u8 {
    with(|c| c.i2c.write(byte) as u8, 0)
}

#[no_mangle]
pub extern "C" fn psdpanel_i2c_read() -> u8 {
    with(|c| c.i2c.read(), 0xff)
}

#[no_mangle]
pub extern "C" fn psdpanel_i2c_stop() {
    with(|c| c.i2c.stop(), ())
}

/// jede simulierte Millisekunde; liefert die Tasten (Bit 0 SEL, 1 NEXT, 2 PREV)
#[no_mangle]
pub extern "C" fn psdpanel_tick(sim_ms: f64) -> u8 {
    with(|c| { c.link.tick(sim_ms); c.link.keys() }, 0)
}

/// Spannung der Tastenleiter in mV fuer die Tasten
#[no_mangle]
pub extern "C" fn psdpanel_key_mv(keys: u8) -> u32 {
    crate::key_mv(keys)
}

/// ATtiny25-Register IO_IEC (Freigabe des IEC-Ports J4)
#[no_mangle]
pub extern "C" fn psdpanel_io_iec() -> u8 {
    with(|c| c.link.io_iec() as u8, 1)
}

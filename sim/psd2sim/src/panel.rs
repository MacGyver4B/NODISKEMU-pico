//! Bedienfeld des petSD+ (Board petsd2_dip40) fuer psd2sim: das Modell
//! steckt in sim/psdpanel (auch von petsdsim genutzt), hier nur die
//! Anbindung an picoem:
//!   - LCD an GPIO 16/17/18/2 (DB4-DB7), 13 (RS), 14 (E)
//!   - Tasten an ADC0 (GPIO 40) hinter dem Teiler 56k/100k
//!   - I2C1: ATtiny25 (0x26) und DS3231 (0x68)

use rp2350_emu::peripherals::i2c::I2cTarget;
use rp2350_emu::Emulator;

pub use psdpanel::{hold_keys, new_panel, serve, Panel};
use psdpanel::{I2cDev, Link};

// Belegung wie pico/boards/petsd2_dip40.h
const LCD_DB: [u32; 4] = [16, 17, 18, 2];
const LCD_RS: u32 = 13;
const LCD_E: u32 = 14;
const ADC_CH: u32 = 0;      // GPIO 40

struct I2cBus(I2cDev);

impl I2cTarget for I2cBus {
    fn start(&mut self, addr: u8, read: bool) -> bool { self.0.start(addr, read) }
    fn write(&mut self, byte: u8) -> bool { self.0.write(byte) }
    fn read(&mut self) -> u8 { self.0.read() }
    fn stop(&mut self) { self.0.stop() }
}

/// ADC-Rohwert (12 Bit hinter dem Teiler) fuer eine Tastenkombination
pub fn key_raw(keys: u8) -> u16 {
    // Pinspannung c/1024*5 V, Teiler 100/156, ADC 4096 bei 3,3 V
    let c = psdpanel::key_level10(keys);
    let raw = (c as f64 / 1024.0 * 5.0 * 100.0 / 156.0 / 3.3 * 4096.0).round();
    raw.min(4095.0) as u16
}

/// Leitet Pins und I2C des Emulators auf das Bedienfeld
pub struct PanelLink {
    link: Link,
}

impl PanelLink {
    pub fn attach(emu: &mut Emulator, p: Panel, verbose: u32) -> Self {
        emu.i2c_attach_target(1, Box::new(I2cBus(I2cDev::new(p.clone(), verbose))));
        let link = Link::new(p);
        emu.adc_set_input(ADC_CH, Some(key_raw(link.keys())));
        PanelLink { link }
    }

    /// nach jedem Emulatorschritt; out/oe: Ausgangspins (pins_out)
    pub fn poll(&mut self, out: u64, oe: u64) {
        let e = (oe >> LCD_E) & 1 == 1 && (out >> LCD_E) & 1 == 1;
        let rs = (out >> LCD_RS) & 1 == 1;
        let mut nib = 0u8;
        for (i, &pin) in LCD_DB.iter().enumerate() {
            if (out >> pin) & 1 == 1 { nib |= 1 << i; }
        }
        self.link.lcd_pins(e, rs, nib);
    }

    /// seltener aufrufen (z. B. jede simulierte Millisekunde)
    pub fn tick(&mut self, emu: &mut Emulator, sim_ms: f64) {
        if let Some(k) = self.link.tick(sim_ms) {
            emu.adc_set_input(ADC_CH, Some(key_raw(k)));
        }
    }

    pub fn io_iec(&self) -> bool {
        self.link.io_iec()
    }
}

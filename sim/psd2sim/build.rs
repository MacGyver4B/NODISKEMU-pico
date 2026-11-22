// simlink.c kommt aus dem VICE-Fork VICE-LRE-SIMLINK (src/simlink);
// Pfad per SIMLINK_DIR, Vorgabe: ../vice-lre-simlink/vice/src/simlink
// (das Repo neben psd2sim als sim/vice-lre-simlink auschecken oder verlinken)
fn main() {
    let sl = std::env::var("SIMLINK_DIR")
        .unwrap_or_else(|_| "../vice-lre-simlink/vice/src/simlink".to_string());
    let os = std::env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();
    println!("cargo:rerun-if-env-changed=SIMLINK_DIR");
    let mut b = cc::Build::new();
    b.file(format!("{}/simlink.c", sl)).include(&sl).flag_if_supported("-std=c11");
    if os != "windows" {
        b.define("_POSIX_C_SOURCE", "200809L");
    }
    if os == "macos" {
        b.define("_DARWIN_C_SOURCE", None);
    }
    b.compile("simlink");
    cc::Build::new().file("sdmodel.c").compile("sdmodel");
    println!("cargo:rerun-if-changed=sdmodel.c");
    println!("cargo:rerun-if-changed={}/simlink.c", sl);
    if os != "windows" {
        println!("cargo:rustc-link-lib=pthread");
    }
    if os == "linux" {
        println!("cargo:rustc-link-lib=rt");
    }
}

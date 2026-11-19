// simlink.c kommt aus dem VICE-Fork (Zweig simlink, src/simlink);
// Pfad per SIMLINK_DIR, Vorgabe: ../vice-lre-simlink/vice/src/simlink
fn main() {
    let sl = std::env::var("SIMLINK_DIR")
        .unwrap_or_else(|_| "../vice-lre-simlink/vice/src/simlink".to_string());
    println!("cargo:rerun-if-env-changed=SIMLINK_DIR");
    cc::Build::new()
        .file(format!("{}/simlink.c", sl))
        .include(&sl)
        .flag("-std=c11")
        .define("_POSIX_C_SOURCE", "200809L")
        .compile("simlink");
    cc::Build::new().file("sdmodel.c").compile("sdmodel");
    println!("cargo:rerun-if-changed=sdmodel.c");
    println!("cargo:rerun-if-changed={}/simlink.c", sl);
    println!("cargo:rustc-link-lib=pthread");
    println!("cargo:rustc-link-lib=rt");
}

use cbindgen::{Config, Language, SortKey};
use std::path::Path;

fn main() {
    let crate_dir = std::env::var("CARGO_MANIFEST_DIR").unwrap();
    let output_file = Path::new("../../nano/lib/rsnano.hpp");

    let config = Config {
        language: Language::Cxx,
        include_guard: Some("rs_nano_bindings_hpp".to_string()),
        namespace: Some("rsnano".to_string()),
        sort_by: SortKey::Name,
        ..Default::default()
    };

    cbindgen::generate_with_config(crate_dir, config)
        .unwrap()
        .write_to_file(output_file);

    println!("cargo:rerun-if-changed=src");
}

with import <nixpkgs> {}; {
  qpidEnv = stdenvNoCC.mkDerivation {
    name = "my-clang-environment";
    buildInputs = [
      pkgs.clang_11
      pkgs.llvm_11
      pkgs.cmake
      #pkgs.libusb
      #pkgs.rtl-sdr
      pkgs.pkg-config
      pkgs.cppcheck
      pkgs.ninja
      pkgs.gcc10
    ];
    shellHook = ''
      export CC=clang
      export RANLIB=llvm-ranlib
      export AR=llvm-ar
    '';
  };
}

with import <nixpkgs> {}; {
  qpidEnv = stdenvNoCC.mkDerivation {
    name = "gcc10-style-fixes-rtl433";
    buildInputs = [
      pkgs.gcc10
      pkgs.cmake
      #pkgs.pkg-config
      #pkgs.cppcheck
      pkgs.ninja
      #pkgs.rtl-sdr
      #pkgs.libusb1
      #pkgs.lld
      pkgs.which
      pkgs.vim
    ];
    shellHook = ''
      export CC=gcc
      export AR=gcc-ar
      export RANLIB=gcc-ranlib
      export NM=gcc-nm
    '';
  };
}

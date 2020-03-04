{ pkgs ? import <nixpkgs> {} }:
let
  stdenv = pkgs.overrideCC pkgs.stdenv pkgs.clang_6;
in rec {
  enableDebugging = false; #true;

  fsmetad = stdenv.mkDerivation {
    name = "fsmetad";
    dontStrip = enableDebugging;
    IS_DEV = enableDebugging;
    srcs = [./src ./ac];
    sourceRoot = "src";
    buildInputs = [
      pkgs.cmake
      pkgs.openssl_1_1
      pkgs.gperftools
    ];
    #cmakeFlags = [
      #"-DCMAKE_BUILD_TYPE=Debug"
    #];
    enableParallelBuilding = true;
  };
}

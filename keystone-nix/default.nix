let
  nixpkgsSrc = import ./nixpkgsSrc.nix;
  pkgs = import nixpkgsSrc {};
  lib = pkgs.lib;

  targetPkgs = pkgs.pkgsCross.riscv64;

  patchedQemu = (pkgs.qemu.overrideAttrs (oldAttrs: {
    patches = (oldAttrs.patches or []) ++ [
      ./0001-Add-keystone-rom-support.patch
    ];
  })).override (oldParams: oldParams // {
    hostCpuTargets = [ "riscv64-softmmu" ];
  });

  # Netboot currently has some assumptions about the target being
  # either x86 or aarch64. Patch this module to avoid adding grub2 &
  # syslinux for non-aarch64 systems:
  patchedNetboot = pkgs.runCommand "patched-nixos-netboot" {} ''
    ${pkgs.patch}/bin/patch -o $out ${
      # Using `modulesPath` here instead of `<nixos/...>` ensures that
      # we're using the modules of our desired Nixpkgs revision,
      # instead of the system-wide one on the build machine.
      "${nixpkgsSrc}/nixos/modules/installer/netboot/netboot.nix"} ${
        pkgs.writeText "nixos-netboot.patch" (builtins.readFile
          ./0001-nixos-netboot-avoid-adding-grub2-syslinux-to-non-aar.patch)}
  '';

  targetSystem = import "${nixpkgsSrc}/nixos" {
    # Note that `config.nix` contains a function, which takes an
    # attribute set (the inputs) and returns an attribute set (the
    # configuration). This is the type expected to be passed into the
    # `configuration` paramter of the $nixpkgs/nixos/default.nix
    # expression. In fact, `nix-rebuild` works quite similar to what
    # we're doing here!
    #
    # We are, however, passing in some part of the host package set
    # manually, to allow patching NixOS modules on the fly and
    # importing them while avoiding infinite loops. This should be
    # removed if possible:
    configuration = import ./config.nix {
      hostPackages = pkgs;
      hostModulesPath = "${nixpkgsSrc}/nixos/modules";
    };
  };

  targetInitrd = tsys: let
    rootPaths =
        tsys.config.netboot.storeContents;

    nixPathRegistration =
      "${targetPkgs.closureInfo { inherit rootPaths; }}/registration";
  in
    targetPkgs.makeInitrdNG {
      inherit (tsys.config.boot.initrd) compressor;
      prepend = [ "${tsys.config.system.build.initialRamdisk}/initrd" ];

      # When Nix store is mounted via 9pfs, we can avoid including a SquashFS:
      #
      contents = [{
        object = nixPathRegistration;
        symlink = "/nix-path-registration";
      }];

      # When the Nix store should be included in the ramdisk as a SquashFS:
      #
      # contents = [{
      #   object =
      #     targetPkgs.callPackage <nixpkgs/nixos/lib/make-squashfs.nix> {
      #       storeContents = tsys.config.netboot.storeContents;
      #     };
      #   symlink = "/nix-store.squashfs";
      # }];
    };

  qemuBaseCommand = { kernel, initrd, cmdlineFile ? null, cmdline ? null, vmstateDisk ? false }: lib.concatStringsSep " " ([
    "${patchedQemu}/bin/qemu-system-riscv64"
    "-machine" "virt,rom=${builtins.path {
      path = ./keystone-artifacts/bootrom.bin;
      name = "keystone_bootrom.bin";
    }}"
    "-bios" "${builtins.path {
      path = ./keystone-artifacts/fw_jump.elf;
      name = "keystone_fw_jump.elf";
    }}"
    "-m" "32G"
    "-smp" "1"
    "-kernel" "${kernel}"
    "-append" (
      if cmdlineFile != null then
        ''"$(cat ${cmdlineFile})"''
      else
        ''"$(cat ${pkgs.writeText "encapfn-qcow-booted-cmdline" cmdline})"''
    )
    "-initrd" "${initrd}"
    "-netdev" "user,id=n0,hostfwd=tcp::5826-:5826"
    "-device" "virtio-net-device,netdev=n0"
    "-device" "virtio-9p-device,id=local9p,fsdev=localfs,mount_tag=local"
    "-fsdev" "local,id=localfs,path=./,security_model=none,writeout=immediate"
    "-device" "virtio-9p-device,id=nixstore9p,fsdev=nixstorefs,mount_tag=nixstore"
    "-fsdev" "local,id=nixstorefs,path=/nix/store,security_model=none,writeout=immediate"
  ] ++ (if vmstateDisk then [
    "-drive" "if=none,format=qcow2,file=vmstate.qcow2"
  ] else []));

  bootedQemuSnapshot = qemuBaseCommand:
    pkgs.stdenvNoCC.mkDerivation {
      name = "encapfn-qcow-booted";

      # Only need a single Python script
      dontUnpack = true;

      buildInputs = with pkgs; [
        patchedQemu
        (python3.withPackages (pypkgs: with pypkgs; [
          pexpect
        ]))
      ];

      buildPhase = ''
        qemu-img create -f qcow2 vmstate.qcow2 0M
        python3 ${
          pkgs.writeText "encapfn-qemu-boot-snapshot" (
            builtins.readFile ./qemu-boot-snapshot.py)} ${
          pkgs.writeScript "encapfn-qcow-booted-cmd" ''
            #!${pkgs.bash}/bin/bash
            ${qemuBaseCommand} -nographic -monitor unix:qemu-monitor-socket,server,nowait
          ''} ./qemu-monitor-socket
        sha256sum ./vmstate.qcow2
      '';

      installPhase = ''
        mkdir -p $out
        mv ./vmstate.qcow2 $out/
      '';

      postInstall = "";

      dontStrip = true;
    };

  initrd = "${targetInitrd targetSystem}/initrd";
  kernel = "${targetSystem.config.system.build.kernel}/Image";
  cmdline = "init=${targetSystem.config.system.build.toplevel}/init ${
    toString targetSystem.config.boot.kernelParams}";
  qemucmd = qemuBaseCommand { inherit initrd kernel cmdline; };

in
  (pkgs.linkFarm "nixos-netboot" [
    {
      name = "initrd";
      path = initrd;
    }
    {
      name = "Image";
      path = kernel;
    }

    # Kernel command line, referenced by the qemucmd script
    {
      name = "cmdline";
      path = pkgs.writeText
        "${targetSystem.config.networking.hostName}-cmdline"
        "init=${targetSystem.config.system.build.toplevel}/init ${
          toString targetSystem.config.boot.kernelParams}";
    }

    # Prepare a booted VM snapshot:
    # {
    #   name = "vmstate.qcow2";
    #   path = "${bootedQemuSnapshot qemucmd}/vmstate.qcow2";
    # }

    # Build the command line as a script, using a relative path to the
    # booted VM snapshot (expected to be in the current directory), as
    # QEMU requires RW access to that image. Also, append the -loadvm
    # option to boot from the created snapshot.
    {
      name = "run-snapshot";
      path = pkgs.writeScript "qemucmd" ''
        #!${pkgs.bash}/bin/bash
        exec ${qemuBaseCommand {
          inherit kernel initrd;
          cmdlineFile = "./result/cmdline";
          vmstateDisk = true;
        }} -loadvm booted -nographic
      '';
    }

    {
      name = "run";
      path = pkgs.writeScript "qemucmd" ''
        #!${pkgs.bash}/bin/bash
        exec ${qemuBaseCommand {
          inherit kernel initrd;
          cmdlineFile = "./result/cmdline";
          vmstateDisk = false;
        }} -nographic
      '';
    }
  ])

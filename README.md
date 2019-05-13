# vm

The program to run virtual machines and run programs in their virtual environment.
This is a wrapper for qemu that helps to have multiple configurations for qemu as
a git-config-like file.

# Require

- qemu;
- libshell (https://github.com/legionus/libshell);
- gcc (for simple init);

# Get started

To initialize home:

```shell
$ vm init
```

To create new profile:

```shell
$ vm setup kernel-linus
```

For kernel developers:
```shell
$ vm setup kernel-linus
$ git config -f ~/.vmconfig vm.kernel-linus.kernel find,/path/to/linux/source/tree
$ vm setup kernel-linus --kernel-requires >> /path/to/linux/source/tree/.config
```

Edit ~/.vmconfig if needed.

To run some useful script from your home:

```shell
$ vm sandbox kernel-linus /host/home/$USER/tmp/testcase.sh
```

or get shell inside:

```shell
$ vm sandbox kernel-linus
```

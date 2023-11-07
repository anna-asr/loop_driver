# Loopback Linux Device Driver

## Overview

This project implements a simple loopback Linux device driver. The driver allows users to write data to the loopback device, and the written data is then hex-dumped into an output file.

## Features

- **Loopback Driver:** Create a loopback device `/dev/loop` that echoes the written data.
- **Hex-dump:** Hex-dump the written data into an output file `/tmp/output`.
- **File Operations:** Implement open, release, read, and write operations for the loopback device.

## Usage

### Build and Install

1. Compile the module:

    ```bash
    make
    ```

2. Insert the module:

    ```bash
    sudo insmod driver.ko
    ```

### Write Data

To write data to the loopback device, use the following command:

```bash
cat my_file.bin > /dev/loop
```

### Test Program

To test the program use the following commands

```bash
cat my_file.bin > /dev/loop

hexdump my_file.bin > /tmp/output1

diff /tmp/output /tmp/output1
```
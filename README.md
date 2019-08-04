# xcp

**xcp** is a small utility like `scp`, aimed at fast large file copying over network.

It resembles `scp` or `rsync` in command line, but makes use of multiple TCP connections to achieve a higher bandwidth.

**Supported platforms:** Linux, Windows

<br>

**This project is under rapid development.**
**Please see [dev](https://github.com/WenbinHou/xcp/tree/dev) branch for details.**

## Usage

Currently it only supports **copy a single file** from local to remote, or from remote to local.

### xcp
````
xcp client
Usage: xcp [OPTIONS] from to

Positionals:
  from <from> REQUIRED        Copy from this path
  to <to> REQUIRED            Copy to this path

Options:
  -h,--help                   Print this help message and exit
  -V,--version                Print version and exit
  -q,--quiet                  Be more quiet
  -v,--verbose                Be more verbose
  -P,-p,--port <port>         Server portal port to connect to
  -r,--recursive              Transfer a directory recursively
  -u,--user <user>            Relative to this user's home directory on server side
  -B,--block <size>:SIZE [b, kb(=1024b), ...]
                              Transfer block size
````

### xcpd
````
xcp server
Usage: xcpd [OPTIONS]

Options:
  -h,--help                   Print this help message and exit
  -V,--version                Print version and exit
  -q,--quiet                  Be more quiet
  -v,--verbose                Be more verbose
  -P,-p,--portal <endpoint>   Server portal endpoint to bind and listen
  -C,--channel <endpoint> ... Server channels to bind and listen for transportation
````

## Notice

- **For better performance, xcp does NOT encrypt its connections, nor does it use any authentication.<br>
  <u>USE IN TRUSTED ENVIRONMENT ONLY!</u>**

- **xcp is NOT designed for high concurrency.**<br>
  Having many clients <span style="color:gray">*(like more than 100 connections in total)*</span> connected to one server simultaneously is likely to get poor performance.

## License

[MIT](./LICENSE) License.

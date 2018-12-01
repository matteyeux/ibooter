# ibooter
A small utility I use with binaries built from iBoot src. 

```
$ Usage : ibooter [OPTIONS]
 -i, --img3 [file] <tag>       create IMG3
 -l, --load [IMG3]             load IMG3 file
 -k, --kickstart [file] <tag>  create and load img3
 -m, --mode                    device mode
 -s, --shell                   start recovery shell
 -d, --diags                   start device in diagnostic mode
 -h, --help                    print help
```

### Dependencies
 - libimobiledevice
 - libreadline
 - libusb
 
### Build
 
Run `make`

### Create IMG3 file 
1) Build iBoot source code
2) Grab binary file (eg iBEC.bin)
3) Run `ibooter [file] <tag>` where `file` is iBEC.bin and `tag` is _ibec_
4) example : `./ibooter -i iBEC.bin ibec`

### Load IMG3 file
Once you have built the IMG3 file, and your device is in kDFU mode run : <br>
`./ibooter -l iBEC.bin.img3`

### Credit

As most of the code is from other repositories, see licenses at the top of C files in `src`. <br>
If I broke licenses feel free to open a issue so I can fix it. 

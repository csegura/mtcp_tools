# ftp_server / telnet_server

## Warning

These tools are quick-and-dirty solutions, not meant for serious or long-term use. It also serves as a subtle reminder that they should be used cautiously due to their lack of security features :-)

I wanted to have two fast programs to communicate with my old computers, from my linux box, I have three old pcs and I'm using mtcp on them. mTcp includes an ftp client and a telnet client, of course, I could use any of the existing ftp servers or telnet ones, but I don´t want to have more services running, also I thought that would be an opportunity to learn something.

## ftp_server

```
ftp_server [port] [ip]
```

If port is not specified default (21) is used, and default IP the first that is not a loopback one. This is a passive ftp sever with anonymous access. Security wasn`t have been a prority. 

## telnet_server

telnet_server runs by default on port 12345, its runs shell.sh as I use zsh I have a little script init.sh to change some shell environments vars. You can change the port passing other as parameter. 


```
telnet_server 9988
```

To use mTcp xmodem / ymodem ensure you have lrzsz installed

```
sz <file> # send file     - Alt+D on mTcp (download)
rb --ymodem # receive file  - Alt+U on mTcp (upload)
```



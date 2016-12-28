[Quiet-lwip](https://github.com/quiet/quiet-lwip/)
===========

This is a binding for [libquiet](https://github.com/quiet/quiet) to [lwip](https://savannah.nongnu.org/projects/lwip/). This binding can be used to create TCP and UDP connections over an audio channel. This channel may be speaker-to-mic ("over the air") or through a wired connection.

This binding provides an abstract version which emits and consumes floating point samples which can be fed to a soundcard. Alternately, it provides a [PortAudio](http://www.portaudio.com/) binding which handles the audio interfacing and works on most modern operating systems.

Build
-----------
With the [dependencies](#dependencies) installed, run `mkdir build && cd build && cmake .. && make`.

Quiet-lwip will build a static library and headers which can then be linked into other programs, allowing them to transmit with sockets via sound.

Examples
-----------

In order to demonstrate that Quiet-lwip can be used in ordinary socket contexts, it comes included with some substantial example programs. These programs demonstrate plausible use cases for quiet-lwip. Although they are fleshed out examples, there are rough edges around error handling in the example code, so take care when copy/pasting them into real products. Always vet your code!

** For convenience, I have run these examples below with client and server on the same laptop. ** If you want to confirm that there is no cheating taking place, you are welcome to inspect how these examples work. Except where noted, we'll be transmitting over the air but using the same speaker and mic, as if two people communicated to each other using the same mouth and ears.

The examples can be built using the build instructions above, followed by `make examples`.

### Discovery

Let's say we have a base station with a speaker and microphone, perhaps a laptop, and we want to connect to this base with a client in order to perform some transactions.

The first order of business would be to discover any nearby servers. We do this with a UDP broadcast packet with payload set to 'MARCO'. The discovery server then replies to the sender with a UDP packet with the contents 'POLO'.

```
quiet-lwip/build $ bin/discovery_server
```

and then

```
quiet-lwip/build $ bin/discovery_client
received response from 192.168.0.8: POLO
```

With that, discovery is complete. Our client has found the server's self-assigned IP address, 192.168.0.8. We'll be targetting this address in the examples that follow.

### Key-Value Store

With the server's IP address found, we turn to our first goal. Here, we'll create a TCP listener on the server on port 7173. This service will allow us associate 32-byte values to 32-byte keys. This example could be extended to practical contexts such as device pairing. It might also be useful for providing small configuration to an embedded device without traditional networking capabilities.

First, we start the server

```
quiet-lwip/build $ bin/kv_server
```

The server has been configured to choose the same address as the discovery_server.

Now we check that it's present with our client. We'll provide it with the address we found from the discovery client.

```
quiet-lwip/build $ bin/kv_client 192.168.0.8 PING
PONG
```

Now let's try establishing a value.

```
quiet-lwip/build $ bin/kv_client 192.168.0.8 ADD:abc=123
ADDED
```

Finally, we fetch it back.

```
quiet-lwip/build $ bin/kv_client 192.168.0.8 GET:abc
123
```

### SOCKS5 Proxy

For this next example, we'll create a [SOCKS5 Proxy](https://en.wikipedia.org/wiki/SOCKS). This could be used to tether one device without networking capabilities to another device with Internet access.

In this example, we'll continue to have a server and client as before, but now the roles will be changed somewhat. Our server will be a typical SOCKS5 Proxy listening on a TCP socket via libquiet and creating requests to remote hosts via native networking. Our client will be essentially a tunnel that listens on its native host on 127.0.0.1:2180 and forwards traffic to our proxy over libquiet. This configuration will allow us to seamlessly pass traffic from a standard tool like curl to a libquiet channel and then through our proxy.

We start by running the server and the client
```
quiet-lwip/build $ bin/proxy_server
```

and

```
quiet-lwip/build $ bin/proxy_client 192.168.0.8
```

Now we're ready to do some surfing on the World Wide Web.

```
$ curl --socks5-hostname 127.0.0.1:2160 https://www.google.com > google.html
  % Total    % Received % Xferd  Average Speed   Time    Time     Time  Current
                                 Dload  Upload   Total   Spent    Left  Speed
100 11020    0 11020    0     0    388      0 --:--:--  0:00:28 --:--:--   794
```

Success! We're running at a blistering ~6400bps (curl reports speeds in Bytes/s, not bits/s).

So far, we've been running these examples over the air. That can be pretty convenient, but if we have an audio cable that can connect to line in/line out, we'll have a much higher-fidelity connection. With the cable plugged in, we change from using the full-duplex pair `audible-7k-channel-0/audible-7k-channel-1` to the half-duplex `cable-64k`. Since we're merely fetching a webpage, half-duplex will work well for us.

After a recompile, and restarting the proxies, we try again.

```
$ curl --socks5-hostname 127.0.0.1:2160 https://github.com > github.html
  % Total    % Received % Xferd  Average Speed   Time    Time     Time  Current
                                 Dload  Upload   Total   Spent    Left  Speed
100 25509    0 25509    0     0   2255      0 --:--:--  0:00:11 --:--:--  3852
```

More than 4x faster. Here, the link itself is capable of sustaining speeds up to 64kbps, but we lose a substantial portion of the throughput to our half-duplex setup and the need for ACKs in our TCP connection.

At this point, with the cable connected, we can even configure our favorite browser to point at our SOCKS proxy. It is, admittedly, about as fast as dial-up, but it will work with sufficient patience.


Configuration
-----------
lwip, on its own, provides substantial configuration. In particular, you'll want to confirm that the settings in `include/lwip/lwip/opt.h` match your desired use case. This file specifies build-time #defines for important characteristics such as memory usage, number of concurrent connections, TCP MSS, and more.

Additionally, more configuration is provided at runtime for the quiet-to-lwip interface in a struct defined in `include/quiet-lwip.h`/`include/quiet-lwip-portaudio.h`. Here you will provide the desired MAC address of the interface as well as the encoder/decoder sample rate and configuration (see also [libquiet's profile system](https://github.com/quiet/quiet#profiles)).

quiet-lwip can be configured to dump every packet it sees in hex format to stdout. This dump, run through `grep "received frame"`, can be then fed to `tools/dump2text.py` and finally Wireshark's `text2pcap -t "%Y-%m-%d %H:%M:%S."` to produce a proper pcap file. This pcap file can be viewed by any pcap viewer such as Wireshark.


Dependencies
-----------
* [libquiet](https://github.com/quiet/quiet) (as well as libquiet's dependencies)
* [PortAudio](http://www.portaudio.com/) (optional)

License
-----------
3-clause BSD.

LWIP is also provided and licensed under its own 3-clause BSD license. Be sure to include lwip's attributions as well.

All dependencies and subdependencies of quiet-lwip, with the exception of [libfec](http://www.ka9q.net/code/fec/), are either BSD or MIT. libfec is licensed under LGPL.

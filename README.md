<h2>gandi-record-updater</h2>

A simple zone file updater algorithm for the <a href="https://www.gandi.net/en">Gandi</a> LiveDNS API.
The program is able to update the zone files of multiple domains (and there subdomains) by passing all required informationen over the command line.


The program has been tested on Debian 10 so far, but should also run on older versions.

Once execution started, the program checks at regular intervals whether the IP address of the system has changed.
If this is the case, it will automatically update the corresponding subdomains of the given domains using the corresponding API keys.
The program then goes back to sleep for a configurable amount of time to unload the CPU.
Thanks to highly efficient programming language C++, the program is perfect for low-performance servers such as the raspberry pi.

<h3>What do I need that for?</h3>

You don't have a static ip address, but you want your homeserver to be permanently accessible.

<h2>Requirements</h2>
<h3>Debian Packages</h3>

All you need is the tool `CURL`, which should already be installed on the most distributions.
If not you can get it by typing `sudo apt-get install curl`.

<h3>API key</h3>

In order to access the zone files, the program needs to know the API key. To get such an key visit https://account.gandi.net/en/ and apply for a production key.

<h2>Installation</h2>

Download the `recUpdater.cpp` file from here as [zip](https://github.com/Kartoffelbauer/gandi-record-updater/releases/download/v1.0/recUpdater.zip)/[tar.gz](https://github.com/Kartoffelbauer/gandi-record-updater/releases/download/v1.0/recUpdater.tar.gz) and extract it.

<h3>[Optional] Compiling</h3>

If you don't want to or can't use the precompiled version (e.g. diffrent architecture), you can compile the `recUpdater.cpp` file yourself by using the following command:

`g++ recUpdater.cpp -o recUpdater.out -lstdc++fs -std=c++17 -pthread`

<h3>Cleaning up</h3>
Its not required but I recommended to put the compiled file (`recUpdater.out`) in a subdirectory, such as `/opt/recUpdater/` so that everything is nice and clean.

Create corresponding directory:
`sudo mkdir /opt/recUpdater`

Move executable to newly created folder:
`sudo mv recUpdater.out /opt/recUpdater/`

<h2>Execution</h2>

With the API key ready we can now look at the syntax of the command.

<strong>Syntax: </strong>`<command> -k <APIKey> -d <domain> <subdomain1> <subdomain2> subdomain...>`

<strong>Parameters:</strong>

`-k` for specifying the API key
<br>`-d` for specifying the Domain followed by all subdomains

<strong>Examples:</strong>

The following will create / update three records called `www`, `mail` and `cloud` for `example.com`:
<br>`./recUpdater.out -k L1K2X3?45D6$89G0P -d example.com www mail cloud`

This will create / update two records called `www` and `mail` for `example1.com` and one record called `cloud` for `example2.de`:
<br>`./recUpdater.out -k L1K2X3?45D6$89G0P -d example1.com www mail cloud -d example2.de cloud`

This example will create / update one record called `www` for `example1.com` with the first API key and one record called `mail` for `example2.de` with the second API key:
<br>`./recUpdater.out -k L1K2X3?45D6$89G0P -d example1.com www mail cloud -k 4LGH%$RTL5HL3D2B9CO -d example2.de cloud`

<h3>Start at boot</h3>

In order to automatically run the program after every system start, you have to create a service file.
To do so type `sudo nano /etc/systemd/system/recUpdater.service` and paste the following:

```
[Unit]
Description=Record updater for gandi.net
After=network.target

[Service]
Type=simple
Restart=on-failure
RestartSec=120
ExecStart=/path/to/executable/recUpdater.out -k 3544558468576543 -d example.com www cloud mail

[Install]
WantedBy=multi-user.target
```

Replace the statement after `ExecStart =` with the path to the executable file, followed by all further details, as already mentioned above.
Save and exit. To activate the service type `sudo service enable recUpdater.service`.

<h2>Configuration</h2>


Before you can configure anything, you must have successfully started the program at least once so that the configuration file was created. To do so see 'Execution' section.
You can find the configuration file at `/etc/recUpdater/recUpdater.conf`. All settings in this file are self-explanatory.

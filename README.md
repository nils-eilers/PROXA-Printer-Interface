PROXA Printer Interface
=======================

This project contains all required files for a modern version of a
vintage printer interface originally made by Helmut Proxa's ULTRA
ELECTRONIC. In the early 80's, it was *the* preferred printer
interface to connect a Centronics compatible printer to a Commodore CBM/PET
to the IEEE-488 bus.

Back then, it was cloned by many other firms several times without permission.
This time, Helmut Proxa kindly allowed me to rebuild his interface and
explained me his very clever circuit in great detail. Though the [original
schematics] (http://www.forum64.de/wbb4/index.php?thread/57196-drucken-mit-cbm-rechnern/&postID=844718#post844718)
were available, I had no access to a dump of the original EPROM contents,
so this part had to been developed again. What's more, I also developed
a command line program for Linux / OS X / Windows which generates the
EPROM contents driven by a configuration file. This enables the user
to re-map the character conversion to suit his personal needs.


Nils Eilers, February 2016

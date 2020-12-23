# Ad Hoc On-Demand Distance Vector routing protocol

2017/2018 **Internet Of Things** Course Project.

### Folders and Makefile

I created the main file "main.c" and placed it in a folder called "project".
This one was in turn in the root folder "Home".

In order to compile the code I created a Makefile containing 
"CONTIKI = ../contiki-2.7", since the contiki-2.7 folder was at the "Home" path
as well.

I added the dependency to the "aodv.c" file (that I placed in the folder 
"project") writing "PROJECT_SOURCEFILES += aodv.c" into the Makefile.


### Installation

In order to run the code from a different location than the one that I used, it
is necessary to change the "CONTIKI = {...}" value on the Makefile and, if 
not in the same folder, also the "aodv.c" dependency.


### ADDITIONAL testing

The LOG is based on "printf" calls made by each node on the crucial parts
of DATA sending.

In order to achieve BETTER LOG RESULTS, it is possible to click the "SKY-BUTTON"
during the simulation (I tried it in Cooja). This will enable the debugger
and the node will execute more detailed "printf" calls. To disable it just
click the button again.

LEDS can also be shown in Cooja. They correspond to: DATA creation, ROUTE_REQ
broadcast, Routing Table entry expiration. 


### Customization

All the "#define" in the "main.c" file can be MODIFIED (they are marked as
"--- MODIFIABLE").
It is better to keep the "TOT_NODES" value equivalent (or higher) than the 
actual number of nodes.

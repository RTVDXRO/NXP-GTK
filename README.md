NXP-GTK
=======

User interface for NXP-TEF668X tuner with Arduino-I2C controller

![Screenshot](/xdr-gtk.png?raw=true)

Copyright (C) 2012-2021  Konrad Kosmatka

https://fmdx.pl/xdr-gtk/

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.
This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

# Build
In order to build XDR-GTK you will need:
- CMake
- C compiler
- GTK+ 2 & dependencies

Once you have all the necessary dependencies, you can use scripts available in the `build` directory.

# Installation
After a successful build, just use:
```sh
$ sudo make install
```
in the `build` directory.

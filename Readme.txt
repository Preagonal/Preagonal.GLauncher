Using the launcher dll will allow you to connect to a different login server with the official graal client

** 

1) Edit license.graal to change the login server location, the first line is the host/ip address, and the second line is the port.

2) Launch the Graal.exe/Worlds.exe/Era.exe app in the Graal folder.

3) Enjoy!

----

- If you have more than one host in license.graal you can hit ctrl+shift+l to activate the selector window on each start of the client.

  host1
  port1
  host2
  port2

- GLauncherW.dll (for the unpacked client only): this also has ctrl+shift+o that shows the console for the client hooking the echo/rpg messages outside of the f2 window.

- version.dll: used for 6.037 and 6.113 (be sure to compile it as x86), should work with or without unpacking the client

- dxgi.dll: Old version of GLauncherW.dll that works with Era/Steam/Normal worlds but still relies on the connector script from con.quattroplay.com so it's detour forwarding is a bit iffy.
  


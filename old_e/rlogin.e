/* rlogin.e - Attempt 50: Portable module with IP-only, added debugging for disconnect */
OPT MODULE
MODULE 'net/netdb', 'net/in', 'socket', 'dos/dos'

/* --- Section: Socket Library Initialisation ---
 * Initialises the bsdsocket.library and ensures the network stack is ready.
 * Includes a retry mechanism for library opening and host ID retrieval.
 * DNS-related code is commented out as we are using IP-only.
 */
PROC waitSocketLib(socketbase:PTR TO LONG, leaveOpen=FALSE)
  DEF n=0, id=0
  DEF dummyHost[255]:STRING, h:PTR TO hostent
  IF socketbase[]=NIL THEN socketbase[]:=OpenLibrary('bsdsocket.library', 2) ELSE leaveOpen:=TRUE
  WriteF('Opening bsdsocket.library, attempt \d\n', n+1)
  WHILE (socketbase[]=NIL) AND (n<60)
    Delay(100)
    n++
    socketbase[]:=OpenLibrary('bsdsocket.library', 2)
    WriteF('Opening bsdsocket.library, attempt \d\n', n+1)
  ENDWHILE
  IF socketbase[]
    n:=0
    WriteF('bsdsocket.library opened, waiting for host ID...\n')
    id:=GetHostId()
    WHILE(id=0) AND (n<60)
      Delay(100)
      n++
      id:=GetHostId()
      WriteF('GetHostId attempt \d, id: \d\n', n+1, id)
    ENDWHILE
    IF id=0
      WriteF('Warning: GetHostId failed after 60 attempts\n')
    ELSE
      WriteF('GetHostId succeeded: \d\n', id)
    ENDIF
    /* Commented out: Dummy DNS lookup to initialise the resolver */
    /* StrCopy(dummyHost, 'google.com') */
    /* WriteF('Performing dummy DNS lookup for \s...\n', dummyHost) */
    /* h := GetHostByName(dummyHost) */
    /* IF h = NIL */
    /*   WriteF('Dummy DNS lookup failed, IoErr: \d\n', IoErr()) */
    /* ELSE */
    /*   WriteF('Dummy DNS lookup succeeded: \s resolved\n', dummyHost) */
    /* ENDIF */
    IF leaveOpen THEN RETURN
    CloseLibrary(socketbase[])
  ENDIF
  socketbase[]:=NIL
ENDPROC

/* --- Section: Rlogin Connection Establishment ---
 * Establishes an rlogin connection to the specified IP and port.
 * Takes an IP address as a LONG, a port number, and a username as parameters.
 * Sets up the socket, connects to the server, and sends the rlogin handshake.
 * Returns the socket descriptor on success, or -1 on failure.
 */
EXPORT PROC rloginConnect(ipAddr:LONG, port:LONG, username:PTR TO CHAR, diface) HANDLE
  DEF sock, sockaddrRaw[16]:ARRAY OF CHAR
  DEF handshake[64]:ARRAY OF CHAR
  DEF term[16]:STRING
  DEF i, handshakeLen
  DEF socketbase=0:LONG
  DEF rloginSocket=-1:LONG
  DEF one=1:LONG
  DEF pos=0, userLen

  waitSocketLib({socketbase}, TRUE)
  IF socketbase=NIL
    WriteF('Error: Failed to open bsdsocket.library\n')
    RETURN -1
  ENDIF

  sockaddrRaw[0] := 16
  sockaddrRaw[1] := 2
  sockaddrRaw[2] := (port >> 8) AND $FF
  sockaddrRaw[3] := port AND $FF
  sockaddrRaw[4] := (ipAddr >> 24) AND $FF
  sockaddrRaw[5] := (ipAddr >> 16) AND $FF
  sockaddrRaw[6] := (ipAddr >> 8) AND $FF
  sockaddrRaw[7] := ipAddr AND $FF
  FOR i := 8 TO 15 DO sockaddrRaw[i] := 0

  WriteF('Creating socket...\n')
  sock := Socket(2, 1, 0)
  IF sock < 0
    WriteF('Error: Socket creation failed: \d\n', sock)
    CloseLibrary(socketbase)
    RETURN -1
  ENDIF
  WriteF('Socket created: \d\n', sock)
  rloginSocket := sock

  IF SetSockOpt(sock, 6, 1, {one}, 4) < 0
    WriteF('Warning: SetSockOpt TCP_NODELAY failed: \d\n', IoErr())
  ENDIF

  WriteF('Connecting to \d.\d.\d.\d:\d...\n', (ipAddr >> 24) AND $FF, (ipAddr >> 16) AND $FF, (ipAddr >> 8) AND $FF, ipAddr AND $FF, port)
  i := Connect(sock, sockaddrRaw, 16)
  WriteF('Connect returned: \d, IoErr: \d\n', i, IoErr())
  IF i < 0
    WriteF('Error: Connect failed\n')
    CloseSocket(sock)
    CloseLibrary(socketbase)
    RETURN -1
  ENDIF

  WriteF('Sending handshake...\n')
  FOR i := 0 TO 63 DO handshake[i] := 0
  
  userLen := StrLen(username)
  WriteF('Username: \s, length: \d\n', username, userLen)
  WriteF('Username bytes: ')
  FOR i := 0 TO userLen-1
    WriteF('\d ', username[i])
  ENDFOR
  WriteF('\n')
  
  pos := 0
  /* First null */
  handshake[pos] := 0
  pos++
  WriteF('After first null, pos: \d\n', pos)
  
  /* Client username (local) */
  FOR i := 0 TO userLen-1
    handshake[pos + i] := username[i]
  ENDFOR
  pos := pos + userLen
  WriteF('After first username, pos: \d\n', pos)
  
  /* Second null */
  handshake[pos] := 0
  pos++
  WriteF('After second null, pos: \d\n', pos)
  
  /* Server username (remote) */
  FOR i := 0 TO userLen-1
    handshake[pos + i] := username[i]
  ENDFOR
  pos := pos + userLen
  WriteF('Handshake after second username: ')
  FOR i := 0 TO pos-1
    IF handshake[i] = 0
      WriteF('0 ')
    ELSE
      WriteF('\d ', handshake[i])
    ENDIF
  ENDFOR
  WriteF('\n')
  
  /* Third null */
  handshake[pos] := 0
  pos++
  WriteF('After third null, pos: \d\n', pos)
  
  /* Terminal type (just "ansi") */
  term[0] := 0
  StrCopy(term, 'ansi')
  WriteF('Term string: \s\n', term)
  FOR i := 0 TO 3  /* Length of "ansi" */
    handshake[pos + i] := term[i]
  ENDFOR
  pos := pos + 4  /* Update position for "ansi" */
  WriteF('After terminal, pos: \d\n', pos)
  
  /* Final null */
  handshake[pos] := 0
  pos++
  WriteF('After final null, pos: \d\n', pos)
  
  handshakeLen := pos
  
  WriteF('Handshake bytes (\d): ', handshakeLen)
  FOR i := 0 TO handshakeLen-1
    IF handshake[i] = 0
      WriteF('0 ')
    ELSE
      WriteF('\d ', handshake[i])
    ENDIF
  ENDFOR
  WriteF('\n')
  
  i := Send(sock, handshake, handshakeLen, 0)
  IF i < 0
    WriteF('Handshake failed: \d\n', i)
    CloseSocket(sock)
    CloseLibrary(socketbase)
    RETURN -1
  ENDIF
  WriteF('Handshake sent: \d bytes\n', i)

  RETURN rloginSocket
EXCEPT
  IF socketbase THEN CloseLibrary(socketbase)
  RETURN -1
ENDPROC

/* --- Section: Rlogin Disconnection ---
 * Closes the rlogin socket and cleans up the socket library.
 * Ensures proper resource deallocation.
 */
EXPORT PROC rloginDisconnect(socketbase:PTR TO LONG, rloginSocket:PTR TO LONG)
  IF rloginSocket[] >= 0
    WriteF('Closing socket \d...\n', rloginSocket[])
    CloseSocket(rloginSocket[])
    WriteF('Socket \d closed\n', rloginSocket[])
    rloginSocket[] := -1
  ENDIF
  IF socketbase[]
    WriteF('Closing bsdsocket.library...\n')
    CloseLibrary(socketbase[])
    WriteF('bsdsocket.library closed\n')
    socketbase[] := NIL
  ENDIF
  WriteF('rloginDisconnect completed\n')
ENDPROC

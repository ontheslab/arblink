/* arblinkdoor_rlogin.e - Transparent Rlogin Door */
MODULE 'AEDoor'        /* AmiExpress Door library */
MODULE 'socket'        /* Socket library bindings */
MODULE 'dos/dos'       /* DOS library for file I/O */
MODULE '*rlogin'       /* Our rlogin module */
MODULE '*stringlist'   /* Stringlist module for config parsing - not used yet */

CONST BUFSIZE=8192     /* 8K buffer */
CONST FIONBIO=$8004667e
CONST VERSION='1.0.0-alpha1'  /* Alpha 1 release */
CONST SUBVERSION='034'        /* Subversion for tracking tweaks */

DEF serverHost[255]:STRING
DEF port=513
DEF socketbase=0:LONG
DEF strfield:PTR TO CHAR  /* Global JHM_String pointer */
DEF diface=0  /* Global diface for AEDoor comms */

/* PROC parseIPAddress: Converts a dotted IP string (e.g., "192.168.0.63") to a 32-bit integer.
 * Takes a string pointer, returns the IP as a LONG or 0 on failure.
 */
PROC parseIPAddress(ipStr:PTR TO CHAR)
  DEF i, pos=0, part=0
  DEF numStr[4]:STRING
  DEF ipParts[4]:ARRAY OF LONG
  DEF success=TRUE
  DEF ipAddr:LONG
  FOR part := 0 TO 3
    numStr[0] := 0
    i := 0
    WHILE (pos < StrLen(ipStr)) AND (ipStr[pos] <> 46) AND (i < 3)
      numStr[i] := ipStr[pos]
      i++
      pos++
    ENDWHILE
    numStr[i] := 0
    ipParts[part] := Val(numStr)
    IF (ipParts[part] < 0) OR (ipParts[part] > 255)
      success := FALSE
      RETURN 0
    ENDIF
    IF part < 3
      IF pos >= StrLen(ipStr) OR (ipStr[pos] <> 46)
        success := FALSE
        RETURN 0
      ENDIF
      pos++
    ENDIF
  ENDFOR
  IF pos <> StrLen(ipStr)
    success := FALSE
    RETURN 0
  ENDIF
  IF success
    ipAddr := Shl(Shl(Shl(ipParts[0],8),8),8) OR Shl(Shl(ipParts[1],8),8) OR Shl(ipParts[2],8) OR ipParts[3]
    RETURN ipAddr
  ENDIF
  RETURN 0
ENDPROC

/* PROC rloginDoor: Handles the Rlogin connection and terminal loop.
 * Using blocking mode for Vampire stability.
 */
PROC rloginDoor(server:PTR TO CHAR, portVal:LONG, username:PTR TO CHAR) HANDLE
  DEF rloginSocket=-1:LONG
  DEF ipAddr:LONG
  DEF buffer[BUFSIZE]:STRING
  DEF input[2]:STRING    /* Single char + null */
  DEF bytesRead:LONG
  DEF done=FALSE
  DEF tempNum[20]:STRING
  DEF tempMsg[255]:STRING
  DEF keyResult:PTR TO CHAR
  DEF keySet[128]:STRING
  DEF sockErr:LONG
  DEF sockErrLen=4:LONG

  ipAddr := parseIPAddress(server)
  IF ipAddr = 0
    WriteStr(diface, 'Error: Invalid IP address in SERVERHOST\n', LF)
    Raise('Invalid IP')
  ENDIF
  StringF(tempNum, '\d', portVal)
  StringF(tempMsg, 'Using IP: \s', server)
  WriteStr(diface, tempMsg, LF)
  StringF(tempMsg, 'Port: \s', tempNum)
  WriteStr(diface, tempMsg, LF)
  StringF(tempMsg, 'Username: \s\n', username)
  WriteStr(diface, tempMsg, LF)

  WriteStr(diface, 'Connecting to server...', LF)
  rloginSocket := rloginConnect(ipAddr, portVal, username, diface)
  IF rloginSocket < 0
    StringF(tempNum, '\d', IoErr())
    StringF(tempMsg, 'Error: Rlogin connection failed, IoErr: \s\n', tempNum)
    WriteStr(diface, tempMsg, LF)
    Raise('Connection failed')
  ENDIF
  StringF(tempNum, '\d', rloginSocket)
  StringF(tempMsg, 'Connected: socket \s\n', tempNum)
  WriteStr(diface, tempMsg, LF)

  WriteStr(diface, 'You are connected to ARBLink - Press <enter> to continue.', LF)
  WriteStr(diface, 'If a screen does not advance - Press <enter>.\n', LF)

  StrCopy(keySet, '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz \n\t\r!@#$%^&*()_+-=[]{}|;:,.<>?`~')

  WHILE NOT done
    bytesRead := Recv(rloginSocket, buffer, BUFSIZE-1, 0)
    IF bytesRead > 0
      buffer[bytesRead] := 0
      WriteStr(diface, buffer, NOLF)
      Delay(5)
    ELSEIF bytesRead = 0
      WriteStr(diface, 'Socket closed\n', '')
      done := TRUE
      IF rloginSocket >= 0
        Delay(10)  /* Reduced to avoid Recv error */
        CloseSocket(rloginSocket)
        rloginSocket := -1
      ENDIF
    ELSEIF bytesRead < 0
      StringF(tempNum, '\d', IoErr())
      StringF(tempMsg, 'Recv error, IoErr: \s\n', tempNum)
      WriteStr(diface, tempMsg, '')
      done := TRUE
    ENDIF

    IF NOT done
      keyResult := HotKey(diface, ' ')
      IF keyResult <> NIL
        IF keyResult = -1
          WriteStr(diface, 'Carrier lost\n', '')
          done := TRUE
        ELSE
          input[0] := keyResult
          input[1] := 0
          IF keyResult = '\n' OR keyResult = '\r'
            StrCopy(input, '\n')
          ENDIF
          IF Send(rloginSocket, input, StrLen(input), 0) < 0
            StringF(tempNum, '\d', IoErr())
            StringF(tempMsg, 'Send error for key \s, IoErr: \s\n', input, tempNum)
            WriteStr(diface, tempMsg, '')
            done := TRUE
          ELSE
            Delay(10)  /* Give server time to process */
            IF keyResult = 'q' OR keyResult = 'Q'
              done := TRUE
            ENDIF
          ENDIF
        ENDIF
      ENDIF
    ENDIF

    Delay(10)
  ENDWHILE

cleanup:
  IF rloginSocket >= 0
    rloginDisconnect({socketbase}, {rloginSocket})
  ENDIF
EXCEPT DO
  IF rloginSocket >= 0
    rloginDisconnect({socketbase}, {rloginSocket})
  ENDIF
  IF exception
    StringF(tempMsg, 'Error: \s\n', exception)
    WriteStr(diface, tempMsg, '')
  ENDIF
ENDPROC

/* PROC getAEStringValue: Fetches a string value from AEDoor using GetDT.
 * Takes a key (e.g., DT_NAME=$64) and an output buffer, copies strfield result.
 */
PROC getAEStringValue(valueKey, valueOutBuffer)
  DEF tempMsg[255]:STRING
  GetDT(diface, valueKey, 0)  /* Data=0 to fetch into strfield */
  StrCopy(valueOutBuffer, strfield)  /* Copy immediately */
ENDPROC

/* PROC main: Entry point, sets up libraries, fetches username, and launches Rlogin.
 * Handles initialisation, cleanup, and user prompt before connection.
 */
PROC main() HANDLE
  DEF username[50]:STRING
  DEF tempUsername[50]:STRING
  DEF commandLine[100]:STRING
  DEF tempMsg[255]:STRING
  DEF result:LONG

  StrCopy(serverHost, '192.168.0.63')
  StrCopy(username, 'V4Salex')  /* Default username with prefix */

  IF aedoorbase := OpenLibrary('AEDoor.library', 1)
    diface := CreateComm(arg[])
    IF diface = 0
      WriteF('Error: Unable to open AEDoor.library\n')
      Raise('AEDoor fail')
    ENDIF
  ELSE
    WriteF('Error: Unable to open AEDoor.library\n')
    Raise('AEDoor fail')
  ENDIF

  socketbase := OpenLibrary('bsdsocket.library', 0)
  IF socketbase = 0
    WriteStr(diface, 'Error: Unable to open bsdsocket.library\n', '')
    Raise('Socket fail')
  ENDIF

  strfield := GetString(diface)  /* Set JHM_String pointer */
  GetDT(diface, BB_MAINLINE, 0)
  StrCopy(commandLine, strfield)
  StringF(tempMsg, 'ARBLink Door: \s (sub \s)', VERSION, SUBVERSION)
  WriteStr(diface, tempMsg, LF)

  /* Get username from AEDoor and add V4S prefix */
  strfield[0] := 0  /* Clear strfield */
  getAEStringValue($64, tempUsername)  /* DT_NAME = $64 */
  IF StrLen(tempUsername) > 0
    StringF(username, 'V4S\s', tempUsername)
  ELSE
    WriteStr(diface, 'Username empty or GetDT failed, using default\n', LF)
  ENDIF

  rloginDoor(serverHost, port, username)

EXCEPT DO
  IF diface THEN DeleteComm(diface)
  IF aedoorbase THEN CloseLibrary(aedoorbase)
  IF socketbase THEN CloseLibrary(socketbase)
  IF exception
    WriteF('Error: \s\n', exception)
  ENDIF
ENDPROC

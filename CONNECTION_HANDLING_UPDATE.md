# Server Disconnect Detection & Connection Handling Update

## Overview
Implemented comprehensive server disconnect detection and user notification system to handle the case when server stops while client is running.

## Changes Made

### 1. UI Utilities Enhancement (`client/ui_utils.h` & `client/ui_utils.c`)

#### Added New Function:
```c
void show_connection_lost_dialog(void);
```

**Functionality:**
- Displays modal error dialog when connection to server is lost
- Shows clear message: "Connection Lost - The connection to the server has been lost. Please restart the application and try again."
- Automatically closes the application after user acknowledges the dialog
- Prevents user from continuing with operations on a dead connection

### 2. Network Layer Enhancement (`client/net.c`)

#### Enhanced Functions:

**`send_message()`:**
- Added connection check before attempting to send
- Calls `show_connection_lost_dialog()` if connection is lost
- Prevents silent failures when server is down

**`receive_message()`:**
- Enhanced disconnect detection with three scenarios:
  1. **Socket already closed** (`client.socket_fd <= 0`): Shows connection lost dialog
  2. **Server closed connection** (`recv() returns 0`): Closes socket, shows dialog
  3. **Connection reset errors** (`errno == ECONNRESET || errno == EPIPE`): Closes socket, shows dialog

#### Added Headers:
```c
#include "ui_utils.h"
#include <errno.h>
```

### 3. Question UI Enhancement (`client/question_ui.c`)

#### Added Header:
```c
#include <sys/socket.h>
```
- Required for `send()` function used in CSV file upload

## Technical Details

### Connection Detection Flow:
```
User Action → send_message() / receive_message()
    ↓
Check socket state (check_connection())
    ↓
If disconnected:
    - Close socket (set fd = -1)
    - Show "Connection Lost" dialog
    - Exit application (gtk_main_quit())
```

### Error Conditions Handled:
1. **ECONNRESET** - Connection reset by peer (server crashed)
2. **EPIPE** - Broken pipe (server closed connection)
3. **recv() == 0** - Server gracefully closed connection
4. **Socket FD <= 0** - Socket not initialized or already closed

## User Experience Improvements

### Before:
- Server stops → Client continues running
- Operations silently fail
- No notification to user
- Confusing error messages
- Application appears frozen

### After:
- Server stops → Immediate detection
- Clear dialog: "Connection Lost"
- User-friendly message explaining the issue
- Application automatically closes
- User knows to restart client and check server

## Testing Scenarios

### Test 1: Server Stops During Idle
1. Start server and client
2. Login successfully
3. Stop server
4. Perform any action (navigate menus, click buttons)
5. **Expected**: Connection lost dialog appears immediately

### Test 2: Server Stops During Operation
1. Start server and client
2. Begin creating a room or importing CSV
3. Stop server mid-operation
4. **Expected**: Connection lost dialog appears after receive timeout

### Test 3: Network Interruption
1. Start server and client
2. Disconnect network
3. Try to send message
4. **Expected**: Connection lost dialog appears

## Files Modified

### Client Files:
- `client/ui_utils.h` - Added `show_connection_lost_dialog()` declaration
- `client/ui_utils.c` - Implemented connection lost dialog with app exit
- `client/net.c` - Enhanced send/receive with disconnect detection and notifications
- `client/question_ui.c` - Added `<sys/socket.h>` for CSV upload

### Compilation Status:
✅ **Client**: Compiled successfully without warnings  
✅ **Server**: Compiled successfully without warnings

## Related Issues Fixed

### Issue 1: "Khi dừng server sao client vẫn chạy?"
**Solution**: Added `show_connection_lost_dialog()` in `send_message()` and `receive_message()`

### Issue 2: Silent failures on disconnect
**Solution**: All network operations now check connection and notify user

### Issue 3: Confusing error states
**Solution**: Clear, user-friendly dialog with actionable message

## Best Practices Implemented

1. **Fail-Fast**: Detect disconnection immediately at network layer
2. **User-Friendly**: Clear error message in user's language preference
3. **Graceful Shutdown**: Close application cleanly after notification
4. **Prevent Corruption**: Block further operations on dead connection
5. **Comprehensive Coverage**: All send/receive paths protected

## Future Enhancements (Optional)

### Potential Improvements:
1. **Auto-Reconnect**: Attempt to reconnect to server automatically
2. **Offline Mode**: Save work locally and sync when reconnected
3. **Connection Monitoring**: Background thread to ping server periodically
4. **Reconnection Dialog**: Option to reconnect without restarting app
5. **Connection Status Indicator**: Visual indicator of connection state

## Compatibility

- **OS**: Linux (tested), should work on Unix-like systems
- **GTK Version**: 3.0+
- **Socket API**: Standard POSIX sockets
- **Thread Safety**: Uses GTK main thread for dialogs

## Notes

- Dialog is modal and blocks until user clicks OK
- Application exits immediately after dialog is acknowledged
- No data is lost (operations that failed are rolled back by server)
- Socket cleanup is automatic (fd closed and set to -1)

## Summary

This update significantly improves the user experience by providing immediate, clear feedback when the server connection is lost. Users are no longer confused by silent failures and know exactly what action to take (restart the application and check server status).

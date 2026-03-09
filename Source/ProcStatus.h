#pragma once

/* State graph. Entry states are NotRunning or AlreadyRunning.
   It is not recommended to act on the "Loading" state, except to gray out buttons.

                                   NotRunning
                                       |
                                       |
                                       v
                         +<-------- Started -------->+
                         |             |             |
                         |             |             |
                         v             v             v
 AlreadyRunning ----> Running <---> Loading <---> Reload <---> (Running)
                         |             |             |
                         |             |             |
                         v             v             v
                         +--------> Stopped <--------+
                                       |
                                       |
                                       v
                                 (NotRunning)

*/

enum ProcStatus {
    // The game is not currently running (steady state).
    NotRunning,
    // The game has just started.
    Started,
    // We have just started; the game was already running.
    AlreadyRunning,
    // The game is running (steady state).
    Running,
    // The game is loading (steady state).
    Loading,
    // The game has just reloaded.
    Reload,
    // The game has just stopped.
    Stopped,
};

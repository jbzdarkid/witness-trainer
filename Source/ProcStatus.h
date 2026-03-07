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
    NotRunning,
    Started,
    AlreadyRunning,
    Running,
    Loading,
    Reload,
    Stopped,
};

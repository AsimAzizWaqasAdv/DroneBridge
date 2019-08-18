/*
 *   This file is part of DroneBridge: https://github.com/seeul8er/DroneBridge
 *
 *   Copyright 2019 Wolfgang Christl
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 *
 */

#ifndef DRONEBRIDGE_DB_COMMON_H
#define DRONEBRIDGE_DB_COMMON_H

#include <syslog.h>


#define LOG_SYS_STD(ident, args...) do {\
                                    (ident == LOG_ERR || ident == LOG_NOTICE) ? fprintf(stderr, args) : printf(args);\
                                    syslog(ident, args);\
                                    } while(0)

#endif //DRONEBRIDGE_DB_COMMON_H

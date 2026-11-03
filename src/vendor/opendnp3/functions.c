/*
 * Copyright 2013-2020 Automatak, LLC
 *
 * Licensed to Green Energy Corp (www.greenenergycorp.com) and Automatak
 * LLC (www.automatak.com) under one or more contributor license agreements.
 * See the NOTICE file distributed with this work for additional information
 * regarding copyright ownership. Green Energy Corp and Automatak LLC license
 * this file to you under the Apache License, Version 2.0 (the "License"); you
 * may not use this file except in compliance with the License. You may obtain
 * a copy of the License at:
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/* SolarOS adaptation, 2026: C name lookup from FunctionCode.h. */
#include "dnp3.h"
const char *dnp3_function_name(uint8_t code) {
    switch(code) {
    case 0x0: return "CONFIRM";
    case 0x1: return "READ";
    case 0x2: return "WRITE";
    case 0x3: return "SELECT";
    case 0x4: return "OPERATE";
    case 0x5: return "DIRECT_OPERATE";
    case 0x6: return "DIRECT_OPERATE_NR";
    case 0x7: return "IMMED_FREEZE";
    case 0x8: return "IMMED_FREEZE_NR";
    case 0x9: return "FREEZE_CLEAR";
    case 0xA: return "FREEZE_CLEAR_NR";
    case 0xB: return "FREEZE_AT_TIME";
    case 0xC: return "FREEZE_AT_TIME_NR";
    case 0xD: return "COLD_RESTART";
    case 0xE: return "WARM_RESTART";
    case 0xF: return "INITIALIZE_DATA";
    case 0x10: return "INITIALIZE_APPLICATION";
    case 0x11: return "START_APPLICATION";
    case 0x12: return "STOP_APPLICATION";
    case 0x13: return "SAVE_CONFIGURATION";
    case 0x14: return "ENABLE_UNSOLICITED";
    case 0x15: return "DISABLE_UNSOLICITED";
    case 0x16: return "ASSIGN_CLASS";
    case 0x17: return "DELAY_MEASURE";
    case 0x18: return "RECORD_CURRENT_TIME";
    case 0x19: return "OPEN_FILE";
    case 0x1A: return "CLOSE_FILE";
    case 0x1B: return "DELETE_FILE";
    case 0x1C: return "GET_FILE_INFO";
    case 0x1D: return "AUTHENTICATE_FILE";
    case 0x1E: return "ABORT_FILE";
    case 0x20: return "AUTH_REQUEST";
    case 0x21: return "AUTH_REQUEST_NO_ACK";
    case 0x81: return "RESPONSE";
    case 0x82: return "UNSOLICITED_RESPONSE";
    case 0x83: return "AUTH_RESPONSE";
    case 0xFF: return "UNKNOWN";
    default: return "UNKNOWN";
    }
}

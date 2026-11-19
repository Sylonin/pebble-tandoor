#pragma once

#include "ui.h"

// A full-screen message; Back closes it. The strings are copied.
void msg_win_push(IconId icon, const char *title, const char *body);

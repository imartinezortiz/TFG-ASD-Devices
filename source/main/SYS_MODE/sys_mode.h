#include "../Starter/starter.h"

enum sys_mode
{
    mirror,
    qr_display,
    self_managed,
};

struct sys_mode_state
{
    enum sys_mode mode;
    enum sys_mode tmp_mode;
    int tmp_mode_expiration;
    struct ConfigurationParameters parameters;
};

enum sys_mode get_mode();
void set_mode(enum sys_mode mode);
void get_parameters(struct ConfigurationParameters *parameters);
void set_parameters(struct ConfigurationParameters *parameters);
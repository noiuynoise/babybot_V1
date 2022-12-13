
enum Lights{
    front_left = 0,
    front_right = 1,
    back_left = 3,
    back_right = 2
};

void initBlinker(void);
void setLights(bool l_blinker, bool r_blinker, bool headlight, bool reverse);
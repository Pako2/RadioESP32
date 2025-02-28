struct Config {
	bool default_ = 1;
	uint8_t defvol = 16;
	int8_t bass = 0;
	int8_t mid = 0;
	int8_t treble = 0;
	uint8_t defstat = 0;
	uint8_t seekstep = 30;
    char hostnm[24] = "RadioESP32";
    char ntpServer[24] = "pool.ntp.org";
	int ntpInterval = 15;
    char timeZone[40] = "CET-1CEST,M3.5.0,M10.5.0/3";
    char tzname[24] = "Europe/Prague";
    char dateformat[11] = "dd-mm-yyyy";
	uint8_t backlight1 = 150;
	uint8_t backlight2 = 0;
	uint16_t refr = 100;
	uint8_t scrollgap = 5;
	uint16_t idle = 30;
	uint8_t tmode = 1;
	uint16_t bat0 = 3095;
	uint16_t bat100 = 4095;
	uint8_t lowbatt = 1;
	uint8_t critbatt = 10;
	uint16_t batw = 1000;
	uint8_t sdclock = 1;
	uint8_t calendar = 0;
	uint8_t bclkpin = 255;
	uint8_t doutpin = 255;
	uint8_t wspin = 255;
#if defined(DISP)
	uint8_t dsptype = 0;
	uint8_t angle = 0;
	uint8_t bckpin = 255;
	uint8_t bckinv = 0;
#endif
	uint8_t extpullup = 0;
#if defined(SDCARD)
	uint8_t sdpullup = 0;
	uint8_t sddpin = 255;
#endif
	uint8_t encclkpin = 255;
	uint8_t encdtpin = 255;
	uint8_t encswpin = 255;
	uint8_t irpin = 255;
	uint8_t mutepin = 255;
#if defined(AUTOSHUTDOWN)
	uint8_t onoffipin = 255;
	uint8_t onoffopin = 255;
	uint8_t dasd = 20;
#endif
    IPAddress apaddress = {192, 168, 4, 1};
    IPAddress apsubnet = {255, 255, 255, 0};
    char apssid[33] = "RadioESP32";
    char wdays[7][24];
};

#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <net/if.h>
#include <string.h>

void get_mac(char *mac_addr) {
	int s;
	struct ifreq buffer;
	char mac_part[3];

	s = socket(PF_INET, SOCK_DGRAM, 0);
	memset(&buffer, 0x00, sizeof(buffer));
	strcpy(buffer.ifr_name, "eth0");
	ioctl(s, SIOCGIFHWADDR, &buffer);
	close(s);

	for (s = 0; s < 6; s++) {
		sprintf(mac_part, "%.2X", (unsigned char) buffer.ifr_hwaddr.sa_data[s]);
		if ((int) s == 0) {
			sprintf(mac_addr, "%s", mac_part);
		} else {
			strcat(mac_addr, mac_part);
		}
	}

}

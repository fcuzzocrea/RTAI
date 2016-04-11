/* 
 *
 * Copyright (C) 2016, Marco Morandini <marco.morandini@polimi.it>.
 * Copyright (C) 2006, 2010 Jan Kiszka <jan.kiszka@web.de>.
 *
 * Derived from Jan Kiszka's smictrlv2.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */


#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <locale.h>
#include <ncurses.h>

#include <pci/pci.h>
#include <sys/io.h>

/* Intel chipset LPC (Low Pin Count) bus controller: PCI device=31 function=0 */
#define LPC_DEV             31
#define LPC_FUNC            0

#define PMBASE_B0           0x40
#define PMBASE_B1           0x41

#define SMI_CTRL_ADDR       0x30
#define SMI_STATUS_ADDR     0x34
#define SMI_ALT_GPIO_ADDR   0x38
#define SMI_MON_ADDR        0x40

struct SmiRegisters {
	char name[19];
	uint32_t value;
};


#define N_SMI_REGS 11
struct SmiRegisters smi_regs[N_SMI_REGS] = {
	{.name = "INTEL_USB2_EN_BIT ",	.value = (0x01 << 18)},
	{.name = "LEGACY_USB2_EN_BIT",	.value = (0x01 << 17)},
	{.name = "PERIODIC_EN_BIT   ",	.value = (0x01 << 14)},
	{.name = "TCO_EN_BIT        ",	.value = (0x01 << 13)},
	{.name = "MCSMI_EN_BIT      ",	.value = (0x01 << 11)},
	{.name = "SWSMI_TMR_EN_BIT  ",	.value = (0x01 << 6) },
	{.name = "APMC_EN_BIT       ",	.value = (0x01 << 5) },
	{.name = "SLP_EN_BIT        ",	.value = (0x01 << 4) },
	{.name = "LEGACY_USB_EN_BIT ",	.value = (0x01 << 3) },
	{.name = "BIOS_EN_BIT       ",	.value = (0x01 << 2) },
	{.name = "GBL_SMI_EN_BIT    ",	.value = (0x01)      }
};

/* SMI_EN register: ICH[0](16 bits), ICH[2-5](32 bits) */
// #define INTEL_USB2_EN_BIT   (0x01 << 18) /* ICH4, ... */
// #define LEGACY_USB2_EN_BIT  (0x01 << 17) /* ICH4, ... */
// #define PERIODIC_EN_BIT     (0x01 << 14) /* called 1MIN_ in ICH0 */
// #define TCO_EN_BIT          (0x01 << 13)
// #define MCSMI_EN_BIT        (0x01 << 11)
// #define SWSMI_TMR_EN_BIT    (0x01 << 6)
// #define APMC_EN_BIT         (0x01 << 5)
// #define SLP_EN_BIT          (0x01 << 4)
// #define LEGACY_USB_EN_BIT   (0x01 << 3)
// #define BIOS_EN_BIT         (0x01 << 2)
// #define GBL_SMI_EN_BIT      (0x01) /* This is reset by a PCI reset event! */

void warning_message(void) {
	int ch, row, col;
	
	char m1[] = "WARNING: this program can permanently damage your computer.";
	char m2[] = "Use it at your own risk: no warranty whatsoever.";
	char m3[] = "You should know what you are doing.";
	char m4[] = "The SMI bits are documented e.g. into";
	char m5[] = "Intel's  I/O Controller Hub 10 (ICH10) Family datasheet";
	char m6[] = "http://www.intel.com/content/www/us/en/io/io-controller-hub-10-family-datasheet.html";
	char m7[] = "PRESS SPACE TO CONTINUE";
	char m8[] = "PRESS q TO QUIT";
	
	getmaxyx(stdscr,row,col);
	mvprintw(row/2-5,(col-strlen(m1))/2,"%s",m1);
	mvprintw(row/2-3,(col-strlen(m2))/2,"%s",m2);
	mvprintw(row/2-1,(col-strlen(m3))/2,"%s",m3);
	mvprintw(row/2+1,(col-strlen(m4))/2,"%s",m4);
	mvprintw(row/2+2,(col-strlen(m5))/2,"%s",m5);
	mvprintw(row/2+3,(col-strlen(m6))/2,"%s",m6);
	mvprintw(row/2+5,(col-strlen(m7))/2,"%s",m7);
	mvprintw(row/2+6,(col-strlen(m8))/2,"%s",m8);

	ch = 't';
	while(ch != 'q') {
		ch = getch();
		if (ch == ' ') {
			clear();
			return;
		}
	}
	endwin();
	exit(1);
}


uint16_t get_smi_en_addr(struct pci_dev *dev, uint8_t gpio)
{
    uint8_t byte0, byte1;

    byte0 = pci_read_byte(dev, PMBASE_B0);
    byte1 = pci_read_byte(dev, PMBASE_B1);

    return ((gpio) ? SMI_ALT_GPIO_ADDR : SMI_CTRL_ADDR) + 
        (((byte1 << 1) | (byte0 >> 7)) << 7); // bits 7-15
}

struct pci_dev * find_smi_device(struct pci_access * pacc) {
	char vendor_name[128];
	char device_name[128];
	struct pci_dev * dev = 0;
	
	for (dev = pacc->devices; dev; dev = dev->next) {
		pci_fill_info(dev, PCI_FILL_IDENT | PCI_FILL_BASES);

		if (dev->vendor_id != PCI_VENDOR_ID_INTEL ||
			dev->device_class != PCI_CLASS_BRIDGE_ISA ||
			dev->dev != LPC_DEV || dev->func != LPC_FUNC)
			continue;

		pci_lookup_name(pacc, vendor_name, sizeof(vendor_name),
				PCI_LOOKUP_VENDOR, dev->vendor_id);
		pci_lookup_name(pacc, device_name, sizeof(device_name),
				PCI_LOOKUP_DEVICE, dev->vendor_id, dev->device_id);

		attron(A_BOLD);
		mvprintw(0, 0, " SMI-enabled chipset found:\n %s %s (%04x:%04x)\n",
			vendor_name, device_name, dev->vendor_id, dev->device_id);
		attroff(A_BOLD);
		refresh();
		return dev;
	}
	printf("No SMI-enabled chipset found\n");

	pci_cleanup(pacc);
	endwin();
	
	return 0;
}


void read_pci(uint16_t * smi_en_addr, uint32_t *value) {
        value[0] = inl(smi_en_addr[0]);
        value[1] = inw(smi_en_addr[1]);
	
	return;
}

void write_pci(uint16_t * smi_en_addr, uint32_t *value) {
        outl(value[0], smi_en_addr[0]);
        outw(value[1], smi_en_addr[1]);
	
	return;
}

void print_bit(int start_row, int * col, uint32_t * val) {
	int i, table;  
	int row;
	int start_reg[2] = {0, 2};
	
#define PRINT_BIT(c,v,f) {                                              \
	mvprintw(row, c, "%20s (0x%08x) = %s",                    \
		f.name, f.value, ((v)&f.value) ? "1" : "0");                     \
}
	for (table = 0; table < 2; table++) {
		row = start_row + start_reg[table];
		for (i = start_reg[table]; i < N_SMI_REGS; i++) {
			PRINT_BIT(col[table], val[table], smi_regs[i]);
       			row++;
		}
	}
// 	PRINT_BIT(INTEL_USB2_EN_BIT);
// 	PRINT_BIT(LEGACY_USB2_EN_BIT);
// 	PRINT_BIT(PERIODIC_EN_BIT);
// 	PRINT_BIT(TCO_EN_BIT);
// 	PRINT_BIT(MCSMI_EN_BIT);
// 	PRINT_BIT(SWSMI_TMR_EN_BIT);
// 	PRINT_BIT(APMC_EN_BIT);
// 	PRINT_BIT(SLP_EN_BIT);
// 	PRINT_BIT(LEGACY_USB_EN_BIT);
// 	PRINT_BIT(BIOS_EN_BIT);
// 	PRINT_BIT(GBL_SMI_EN_BIT);
    
#undef PRINT_BIT
	refresh();
}

int main(int argc, char *argv[]) {

	struct pci_access *pacc;
	struct pci_dev *dev;
	uint16_t smi_en_addr[2];
	uint32_t orig_value[2], new_value[2]; /* [0]: SMI ; [1]: SMI GPIO */
	//int reg_width = 8; /* nybbles */
	//int reg_width_gpio = 4; /* nybbles */
	
	int current_bit = 0;
	int current_smi = 0;

	int ch = ' ';
	int cur_line;
	int table_start_row = 25;
	int table_start_col[2] = {1, 60};
	int bit_col[2] = {37, 96};
	int start_reg[2] = {0, 2};
	int do_stuff = 0;
	int smi_n = 0;

	/* check root */
	if (iopl(3) < 0) {
		printf(" root permissions required\n");
		exit(1);
	}

	pacc = pci_alloc();
	pci_init(pacc);
	pci_scan_bus(pacc);
	
	dev = find_smi_device(pacc);
        smi_en_addr[0] = get_smi_en_addr(dev, 0);
        smi_en_addr[1] = get_smi_en_addr(dev, 1);
	
	read_pci(smi_en_addr, orig_value);
	new_value[0] = orig_value[0];
	new_value[1] = orig_value[1];
	
	while ((ch = getopt(argc,argv,"hg:s:")) != EOF) {
		switch (ch) {
			case 's':
				new_value[0] = strtol(optarg, NULL, (strncmp(optarg, "0x", 2) == 0) ? 16 : 10);
				do_stuff = 1;
				break;
			case 'g':
				smi_n = 1;
				break;
			case 'h':
				fprintf(stderr, "\n");
				fprintf(stderr, "usage: smictrlv3_wrapper [-h] [[-g] [-s <bits>]\n");
				fprintf(stderr, "  -h show this help\n");
				fprintf(stderr, "  -g operate on alternate GPIO SMI_EN\n");
				fprintf(stderr, "  -s sets SMI Registers bits\n");
				fprintf(stderr, "  <bits> are in decimal or 0xHEX\n\n");
				fprintf(stderr, "  without arguments: interactive terminal application\n");
				fprintf(stderr, "  \n");
				fprintf(stderr, "WARNING: this program can permanently damage your computer.\n");
				fprintf(stderr, "Use it at your own risk: no warranty whatsoever.\n");
				fprintf(stderr, "\n");

				exit(2);
				break;
			default:
				break;
		}
	}
	
	if (do_stuff) {
		if (smi_n) {
			new_value[1] = new_value[0];
			new_value[0] = orig_value[0];
		}
		write_pci(smi_en_addr, new_value);
		read_pci(smi_en_addr, new_value);
	        fprintf(stderr, " %s register startup value:\t0x%0*x\n", 
			"     SMI_EN", 8, orig_value[0]);
	        fprintf(stderr, " %s register startup value:\t0x%0*x\n", 
			"GPIO SMI_EN", 4, orig_value[1]);
	        fprintf(stderr, " %s register current value:\t0x%0*x\n", 
			"     SMI_EN", 8, new_value[0]);
	        fprintf(stderr, " %s register current value:\t0x%0*x\n", 
			"GPIO SMI_EN", 4, new_value[1]);
		exit(0);
	}

	/* initialize ncurses */
	setlocale(LC_ALL, "");
	initscr(); cbreak(); noecho();
	nonl();
	intrflush(stdscr, FALSE);
	keypad(stdscr, TRUE);

	warning_message();

	attron(A_BOLD);
        mvprintw(4, 0, " %s register startup value:\t0x%0*x\n", 
			"     SMI_EN", 8, orig_value[0]);
        mvprintw(5, 0, " %s register startup value:\t0x%0*x\n", 
			"GPIO SMI_EN", 4, orig_value[1]);
        mvprintw(8, 0, " %s register current value:\t0x%0*x\n", 
			"     SMI_EN", 8, new_value[0]);
        mvprintw(9, 0, " %s register current value:\t0x%0*x\n", 
			"GPIO SMI_EN", 4, new_value[1]);
	attroff(A_BOLD);
	refresh();
	
	attron(A_BOLD);
	mvprintw(11, 1, "USAGE:");
	attroff(A_BOLD);
	mvprintw(13, 1, "SELECT BIT MASK BELOW");
	//mvprintw(4, 1, "      g          SWITCH TO/FROM alt GPIO SMI_EN");
	mvprintw(14, 1, "   a          APPLY BIT MASK");
	mvprintw(15, 1, "   r          RESET TO STARTUP VALUES");
	mvprintw(16, 1, "   q          QUIT");
	mvprintw(17, 1, "   SPACE      TOGGLE");
	mvprintw(18, 1, "   ARROW KEYS NAVIGATE");
	attron(A_BOLD);
	//mvprintw(4, 7, "g");
	mvprintw(14, 4, "a");
	mvprintw(15, 4, "r");
	mvprintw(16, 4, "q");
	mvprintw(17, 4, "SPACE");
	mvprintw(18, 4, "ARROW KEYS");
	attroff(A_BOLD);

	attron(A_BOLD);
	mvprintw(21, 1, "CURRENT BIT MASK");
	mvprintw(23, 1, "SMI_EN:");
	mvprintw(23, 60, "ALT GPIO SMI_EN:");
	attroff(A_BOLD);
	

//	attron(A_BOLD | A_REVERSE);
//	mvprintw(cur_line, cur_col, "X");
	print_bit(table_start_row, table_start_col, new_value);
	cur_line = table_start_row;
	move(cur_line, bit_col[current_smi]);
//	attroff(A_REVERSE);
	refresh();
	
	while(ch != 'q') {
		ch = getch();
		switch(ch) {
			case KEY_RIGHT:
				if (current_bit >= start_reg[1]) {
					current_smi = 1;
					move(cur_line, bit_col[current_smi]);
					refresh();
				}
				break;
			case KEY_LEFT:
				current_smi = 0;
				move(cur_line, bit_col[current_smi]);
				refresh();
				break;
			case KEY_UP:
				if (current_bit != start_reg[current_smi]) {
					cur_line--;
					current_bit--;
					move(cur_line, bit_col[current_smi]);
					refresh();
				}
				break;
			case KEY_DOWN:
				if (current_bit != 10) {
					cur_line++;
					current_bit++;
					move(cur_line, bit_col[current_smi]);
					refresh();
				}
				break;
			case ' ':
				new_value[current_smi] = new_value[current_smi]^smi_regs[current_bit].value;
				attron(A_BOLD);
			        mvprintw(8, table_start_col[1], " %s register required value:\t0x%0*x\n", 
					"     SMI_EN", 8, new_value[0]);
			        mvprintw(9, table_start_col[1], " %s register required value:\t0x%0*x\n", 
					"GPIO SMI_EN", 4, new_value[1]);
				attroff(A_BOLD);
				mvprintw(cur_line, bit_col[current_smi], new_value[current_smi]&smi_regs[current_bit].value?"1":"0");
				move(cur_line, bit_col[current_smi]);
				refresh();
				break;
			case 'r':
				write_pci(smi_en_addr, orig_value);
				read_pci(smi_en_addr, new_value);
								
				attron(A_BOLD);
			        mvprintw(8, 0, " %s register current value:\t0x%0*x\n", 
					"     SMI_EN", 8, new_value[0]);
			        mvprintw(9, 0, " %s register current value:\t0x%0*x\n", 
					"GPIO SMI_EN", 4, new_value[1]);
				attroff(A_BOLD);
				print_bit(table_start_row, table_start_col, new_value);
				move(cur_line, bit_col[current_smi]);
				refresh();
				break;
			case 'a':
				write_pci(smi_en_addr, new_value);
				read_pci(smi_en_addr, new_value);
				
				attron(A_BOLD);
			        mvprintw(8, 0, " %s register current value:\t0x%0*x\n", 
					"     SMI_EN", 8, new_value[0]);
			        mvprintw(9, 0, " %s register current value:\t0x%0*x\n", 
					"GPIO SMI_EN", 4, new_value[1]);
				attroff(A_BOLD);
				print_bit(table_start_row, table_start_col, new_value);
				move(cur_line, bit_col[current_smi]);
				refresh();
				break;
			default:
				break;
		}
	}

	pci_cleanup(pacc);
	endwin();
	
	return 0;
}

#define _CRT_SECURE_NO_WARNINGS
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>

typedef uint8_t memoryByte;

// k�dmem�ria nem val�s�gh�, de a szimul�ci�hoz megfelel�
char codeMemory[1024][40];

// bitc�mezhet� adatmem�ria
typedef union bitAddressable{
	struct bit {
		uint8_t d0 : 1;
		uint8_t d1 : 1;
		uint8_t d2 : 1;
		uint8_t d3 : 1;
		uint8_t d4 : 1;
		uint8_t d5 : 1;
		uint8_t d6 : 1;
		uint8_t d7 : 1;
	};
	uint8_t data8bit;
}bitAddressable;	

// a regiszterbankok is a RAM mem�ria egy r�sz�t k�pzik
typedef struct regBank {
	memoryByte reg[8];
}regBank;

// adatmem�ria c�mek:
memoryByte *regsRAM[32];					// regiszterek
bitAddressable *baRAM[32];					// bitc�mezhet� mem�riatartom�ny
memoryByte *normalRAM[192];					// nem bitc�mezhet� norm�l mem�riatartom�ny
void* address[256];							// a teljes bels� mem�ria (256 byte)

// adatmem�ria rekeszek:
memoryByte normalData[182];
bitAddressable baData[16];
bitAddressable baReg[5];

// special function regiszterek:
bitAddressable P0;
bitAddressable P1;
bitAddressable P2;
bitAddressable P3;
bitAddressable TCON;
bitAddressable SCON;
bitAddressable IP;
bitAddressable IE;
bitAddressable PSW;
bitAddressable ACC;
bitAddressable B;
memoryByte SP;
memoryByte DPL;
memoryByte DPH;
memoryByte PCON;
memoryByte TMOD;
memoryByte TL0;
memoryByte TL1;
memoryByte TH0;
memoryByte TH1;
memoryByte SBUF;

// regiszterbankok (�sszesen 32 db regiszter)
regBank bank[4];
// aktu�lis regiszterbank kiv�laszt�s�ra szolg�l� v�ltoz�
unsigned int bankSelect = 0;

// �llapotg�phez tartoz� elemek:
typedef char* param;
typedef void fc(param s);
typedef fc* f;

// �llapotok
typedef enum { S, BS, Wbit, WbitJ, WL, WO1, WO2, WO0, WAB, WA, WAC, ERR, EXE, END_CODE } state;

// esem�nyek
typedef enum { NOP, USING, DSEG, BSEG, SETB, CLR, CPL, RL, RLC, RR, RRC, SWAP, MOV, ADD, ADDC, DJNZ, SUBB, ANL, ORL, XRL, INC, PUSH, POP, DEC, SJMP, CALL, JZ, JNZ, JC, JNC, JB, JNB, MUL, DIV, RET, END, C, R0_I, R1_I, AB, RA, RB, R0, R1, R2, R3, R4, R5, R6, R7, ADR, IMM, LAB, ENC, ERC } in_event;

// �llapot <-> tev�kyenys�g
typedef struct { state n_state;  f tsk; } elem;

// esem�ny <-> param�ter
typedef struct { in_event e; char str[30]; } eve_par;
elem ct[END_CODE + 1][ERC + 1];

// jelz� bit, amely egyesbe billen, ha �j utas�t�s k�vetkezik, egy�bk�n a feldolgoz�s alatt v�gig null�n marad az �rt�ke
int flag = 0;

// az utas�t�sok sz�ma
int codeLength = 0;

// seg = 0 -> adatszegmens,	seg = 1 -> bitszegmens
int seg = 0;

// a c�mk�k t�rol�s�ra szolg�l - egy c�mk�hez pontosan egy c�m tartozik, amely az utas�t�s c�me
typedef struct label {
	uint8_t addr;
	char* lab;
}label;

// c�mk�k t�rol�sa
label labels[255];
// c�mk�k sz�mol�sa
int lbCnt = 0;

// ALU - nem val�s�gh�
typedef struct ALU {
	uint8_t* op1;
	uint8_t op2;
	bitAddressable* baOp;
	uint8_t bit;
	in_event F;
}ALU;
ALU aluUnit;

// a haszn�lhat� utas�t�sokat t�rol� vektor
const char cmdVector[][8] = { "nop", "using", "dseg", "bseg", "setb", "clr", "cpl", "rl", "rlc", "rr", "rrc", "swap", "mov", "add", "addc", "djnz", "subb", "anl", "orl", "xrl", "inc", "push", "pop", "dec", "sjmp", "call", "jz", "jnz", "jc", "jnc", "jb", "jnb", "mul", "div", "ret", "end", "C", "@R0", "@R1", "AB", "A", "B", "R0", "R1", "R2", "R3", "R4", "R5", "R6", "R7" };

// be �s kimeneti txt form�tum� f�jlokhoz l�trehozott f�jl pointerek
FILE *fin, *fout;
//---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
//-------------Az �llapotg�p �ltal haszn�lt f�ggv�nyek-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

// bitc�mezhet� mem�riac�m el��ll�t�sa
uint8_t bitAddressDecode(uint8_t bitAdr) {
	return bitAdr / 8;
}

// hib�s esem�ny kezel�se
void hiba(param s) {
	printf("\nHIBA - a programkod %d. soraban\n", PCON + 1);
	if(s != NULL) printf("Utolso parameter: %s\n", s); 
	exit(EXIT_FAILURE);
}

// a kezdeti �rt�kek be�ll�t�sa null�ra a teljes adatmem�ria ter�leten
void initialization() {
	int i;
	for (i = 0; i < 32; i++) {
		*regsRAM[i] = 0;
		baRAM[i]->data8bit = 0;
	}
	for (i = 0; i < 192; i++) {
		*normalRAM[i] = 0;
	}
}

// egy bitc�mezhet� c�mhez tartoz� index keres�se
unsigned int srcBAAdr(void *ptr) {
	if (ptr == NULL) { return 256; }
	unsigned int i = 0;
	for (; i < 32; i++) {
		if (baRAM[i] == ptr) { return i; }
	}
	return 256;
}

// egy norm�l adatc�mhez tartoz� index keres�se
unsigned int srcNRAdr(void* ptr) {
	if (ptr == NULL) { return 256; }
	unsigned int i = 0;
	for (; i < 192; i++) {
		if (normalRAM[i] == ptr) { return i; }
	}
	return 256;
}

// opreandus el��ll�t�sa indirekt mem�ric�mz�s eset�n
uint8_t* getIdxP(int idx) {
	void* adr = address[idx];
	if (idx < 32) {
		return regsRAM[idx];
	}
	else if (((idx > 31) && (idx < 48)) || (idx > 127)) {
		idx = srcBAAdr(adr);
		if (idx != 256) return &baRAM[idx]->data8bit;
		else {
			idx = srcNRAdr(adr);
			if (idx == 256) return NULL;
			else return normalRAM[idx];
		}
	}
	else {
		idx = srcNRAdr(adr);
		if (idx == 256) return NULL;
		else return normalRAM[idx];
	}
}

// opreandus el��ll�t�sa indirekt mem�ric�mz�s eset�n
unsigned int getIdx(int idx) {
	void* adr = address[idx]; 
	if (idx < 32) {
		return *regsRAM[idx];
	}
	else if (((idx > 31) && (idx < 48)) || (idx > 127)) {
		idx = srcBAAdr(adr);
		if (idx != 256) return baRAM[idx]->data8bit;
		else {
			idx = srcNRAdr(adr);
			if (idx == 256) return idx;
			else return *normalRAM[idx];
		}
	}
	else {
		idx = srcNRAdr(adr);
		if (idx == 256) return idx;
		else return *normalRAM[idx];
	}
}

// bemeneti sz�veg konvert�l�sa esem�nny�
in_event scanCode(const char* str) {
	if (strcmp(str, "") == 0) return ENC;
	unsigned int num;
	in_event ev = NOP;
	int equal = 0;
	while (ev <= R7) {
		if (strcmp(str, cmdVector[ev]) == equal) return ev;
		ev++;
	}
	if (*str == '#') {
		int flag = sscanf(str, "#%d", &num);
		if (flag) {
			if (num < 256) return IMM;
			return ERC;
		}
		return ERC;
	}
	if (*str == '@') {
		if (strcmp(str, cmdVector[R0_I]) == equal) return R0_I;
		if (strcmp(str, cmdVector[R1_I]) == equal) return R1_I;
		else return ERC;
	}
	if (isalpha(*str)) { return LAB; }
	num = strtoul(str, NULL, 0);
	if (num < 256) { return ADR; }
	return ERC;
}

// sztringm�sol� f�ggv�ny: feladata a k�dmem�ri�b�l kiolvasott utas�t�sok t�rdel�se �s m�sol�sa
char* strCopy(char* strTarget, char* str, char* lastPtr) {
	int i = 1;
	int j = 0;
	int k = 0;
	if (str == NULL) return NULL;
	if (lastPtr != NULL) {
		for (j = 0; lastPtr != &str[j]; j++);
	}
	while (i) {
		if (str[j] == '\0') {
			i = 0;
			strTarget[k] = str[j];
			return &str[j];
		}
		if (str[j] == ' ') {
			i = 0;
			strTarget[k] = '\0';
			return &str[j + 1];
		}
		else
			strTarget[k] = str[j];
		j++;
		k++;
	}
}

// az esem�ny �s a hozz� tartoz� bemeneti param�ter el��ll�t�sa
eve_par whatHappened(char **strPtr) {
	eve_par ep;
	if (flag) {
		*strPtr = NULL;
		flag = 0;
	}
	*strPtr = strCopy(ep.str, codeMemory[PCON], *strPtr);
	printf("%s\n", ep.str);
	ep.e = scanCode(ep.str);
	return ep;
}

// c�mke eset�n a programsz�ml�l� l�p egyet el�re a flag pedig jelzi, hogy �j utas�t�s k�vetkezik
void saveLab(param s) {
	PCON++;
	flag = 1;
}

// szegmens kiv�laszt�sa
void sSelect(param s) {
	in_event e = scanCode(s);
	if (e == DSEG) seg = 0;
	else if (e == BSEG) seg = 1;
	PCON++;
	flag = 1;
}

// szubrutinb�l val� visszat�r�s
void retCode(param s) {
	SP--;
	PCON = normalData[SP];
	PCON++;
	flag = 1;
}

// bitc�mz�s eset�n az adott bit tartalm�nak lek�rdez�se
uint8_t bitGet(int idx, bitAddressable* data) {
	switch (idx) {
	case 0:
		return data->d0;
	case 1:
		return data->d1;	
	case 2:
		return data->d2;
	case 3:
		return data->d3;
	case 4:
		return data->d4;
	case 5:
		return data->d5;
	case 6:
		return data->d6;
	case 7:
		return data->d7;
	default:
		return 2;
	}
}

// bitc�mz�s eset�n az adott bit be�ll�t�sa
void bitSet(int idx, bitAddressable* data) {
	switch (idx) {
	case 0:
		data->d0 = 1;
		break;
	case 1:
		data->d1 = 1;
		break;
	case 2:
		data->d2 = 1;
		break;
	case 3:
		data->d3 = 1;
		break;
	case 4:
		data->d4 = 1;
		break;
	case 5:
		data->d5 = 1;
		break;
	case 6:
		data->d6 = 1;
		break;
	case 7:
		data->d7 = 1;
		break;
	default:
		hiba(NULL);
	}
}

// bitc�mz�s eset�n az adott bit be�ll�t�sa
void bitClr(int idx, bitAddressable* data) {
	switch (idx) {
	case 0:
		data->d0 = 0;
		break;
	case 1:
		data->d1 = 0;
		break;
	case 2:
		data->d2 = 0;
		break;
	case 3:
		data->d3 = 0;
		break;
	case 4:
		data->d4 = 0;
		break;
	case 5:
		data->d5 = 0;
		break;
	case 6:
		data->d6 = 0;
		break;
	case 7:
		data->d7 = 0;
		break;
	default:
		hiba(NULL);
	}
}

// bitc�mz�s eset�n az adott bit komplement�l�sa
void bitCpl(int idx, bitAddressable* data) {
	switch (idx) {
	case 0:
		data->d0 = ~data->d0;
		break;
	case 1:
		data->d1 = ~data->d1;
		break;
	case 2:
		data->d2 = ~data->d2;
		break;
	case 3:
		data->d3 = ~data->d3;
		break;
	case 4:
		data->d4 = ~data->d4;
		break;
	case 5:
		data->d5 = ~data->d5;
		break;
	case 6:
		data->d6 = ~data->d6;
		break;
	case 7:
		data->d7 = ~data->d7;
		break;
	default:
		hiba(NULL);
	}
}

// NOP utas�t�s
void nope(param s) {
	PCON++;
	flag = 1;
}

// utas�t�s �rtelmez�se
void unCmd(param s) {
	aluUnit.F = scanCode(s);
}

// �ltal�nos utas�t�s v�grehajt�sa 
void exec(param s) {
	in_event op = aluUnit.F;
	uint16_t res = 0;
	uint8_t bit = 0;
	fprintf(fout, "utas�t�s: ");
	switch (op) {
	case MOV:	
		fprintf(fout, "(0x%02x)MOV		- adatmozgat�s		- fel�l�rt adat: 0x%02x		�j adat: 0x%02x\n", PCON, *aluUnit.op1, aluUnit.op2);	
		*aluUnit.op1 = aluUnit.op2;  break;

	case ADD:	
		fprintf(fout, "(0x%02x)ADD		- �sszead�s		- fel�l�rt adat: 0x%02x		�j adat: 0x%02x + 0x%02x = ", PCON, *aluUnit.op1, *aluUnit.op1, aluUnit.op2);
		if ((*aluUnit.op1 + aluUnit.op2) > 255) PSW.d7 = 1; else PSW.d7 = 0;
		if ((*aluUnit.op1 + aluUnit.op2) > 127) PSW.d2 = 1; else PSW.d2 = 0;
		*aluUnit.op1 += aluUnit.op2; 
		fprintf(fout, "0x%02x\n", *aluUnit.op1); break;

	case ADDC:	
		fprintf(fout, "(0x%02x)ADDC		- �sszead�s(cy)		- fel�l�rt adat: 0x%02x		�j adat: 0x%02x + 0x%02x + cy(%d) = ", PCON, *aluUnit.op1, *aluUnit.op1, aluUnit.op2, PSW.d7);
		if ((*aluUnit.op1 + aluUnit.op2 + PSW.d7) > 255) PSW.d7 = 1; else PSW.d7 = 0;
		if ((*aluUnit.op1 + aluUnit.op2 + PSW.d2) > 127) PSW.d2 = 1; else PSW.d2 = 0;
		*aluUnit.op1 = *aluUnit.op1 + aluUnit.op2 + PSW.d7; 
		fprintf(fout, "0x%02x\n", *aluUnit.op1); break;

	case SUBB:	
		fprintf(fout, "(0x%02x)SUBB		- kivon�s		- fel�l�rt adat: 0x%02x		�j adat: 0x%02x - 0x%02x = ", PCON, *aluUnit.op1, *aluUnit.op1, aluUnit.op2);
		if ((*aluUnit.op1 - aluUnit.op2) < 0) PSW.d7 = 1; else PSW.d7 = 0;
		if ((*aluUnit.op1 - aluUnit.op2) < 128) PSW.d2 = 1; else PSW.d2 = 0;
		*aluUnit.op1 -= aluUnit.op2;
		fprintf(fout, "0x%02x\n", *aluUnit.op1); break;

	case DJNZ:
		fprintf(fout, "(0x%02x)DJNZ		- felt�teles ugr�s		- hivatkozott c�m: 0x%02x fel�l�rt adat: 0x%02x �j adat: = ", PCON, aluUnit.op2, *aluUnit.op1);
		*aluUnit.op1 -= 1;
		if (*aluUnit.op1 != 0) {
			PCON = aluUnit.op2;
		}
		fprintf(fout, "0x%02x\n", *aluUnit.op1); break;

	case JZ:
		fprintf(fout, "(0x%02x)JZ	- felt�teles ugr�s		- hivatkozott k�dc�m: 0x%02x A: 0x%02x\n", PCON, aluUnit.op2, ACC.data8bit);
		if (ACC.data8bit == 0) {
			PCON = aluUnit.op2;
		}
		break;

	case JNZ:
		fprintf(fout, "(0x%02x)JNZ	- felt�teles ugr�s      	- hivatkozott k�dc�m: 0x%02x A: 0x%02x\n", PCON, aluUnit.op2, ACC.data8bit);
		if (ACC.data8bit != 0) {
			PCON = aluUnit.op2;
		}
		break;

	case JC:
		fprintf(fout, "(0x%02x)JC	- felt�teles ugr�s		- hivatkozott k�dc�m: 0x%02x CY: %d\n", PCON, aluUnit.op2, PSW.d7);
		if (PSW.d7) {
			PCON = aluUnit.op2;
		}
		break;

	case JNC:
		fprintf(fout, "(0x%02x)JNC	- felt�teles ugr�s		- hivatkozott k�dc�m: 0x%02x CY: %d\n", PCON, aluUnit.op2, PSW.d7);
		if (!PSW.d7) {
			PCON = aluUnit.op2;
		}
		break;

	case JB:
		fprintf(fout, "(0x%02x)JB	- felt�teles ugr�s		- hivatkozott k�dc�m: 0x%02x adat: 0x%02x (%d. helyi�rt�k) = ", PCON, aluUnit.op2, aluUnit.baOp->data8bit, aluUnit.bit);
		bit = bitGet(aluUnit.bit, aluUnit.baOp);
		if (bit == 2) hiba(s);
		fprintf(fout, "%d\n", bit);
		if (bit) {
			PCON = aluUnit.op2;
		}
		break;

	case JNB:
		fprintf(fout, "(0x%02x)JNB	- felt�teles ugr�s      	- hivatkozott k�dc�m: 0x%02x		adat: 0x%02x (%d. helyi�rt�k) = ", PCON, aluUnit.op2, aluUnit.baOp->data8bit, aluUnit.bit);
		bit = bitGet(aluUnit.bit, aluUnit.baOp);
		if (bit == 2) hiba(s);
		fprintf(fout, "%d\n", bit);
		if (!bit) {
			PCON = aluUnit.op2;
		}
		break;

	case CALL:
		fprintf(fout, "(0x%02x)CALL		- szubrutin h�v�s		- hivatkozott k�dc�m: 0x%02x\n", PCON, aluUnit.op2);
		normalData[SP] = PCON;
		SP++;
		PCON = aluUnit.op2;
		break;

	case SJMP:
		fprintf(fout, "(0x%02x)SJMP	- felt�tlen ugr�s		- hivatkozott k�dc�m: 0x%02x\n", PCON, aluUnit.op2);
		PCON = aluUnit.op2;
		break;

	case ANL:	
		fprintf(fout, "(0x%02x)ANL		- �S m�velet		- fel�l�rt adat: 0x%02x		�j adat: 0x%02x AND 0x%02x = ", PCON, *aluUnit.op1, *aluUnit.op1, aluUnit.op2);
		*aluUnit.op1 &= aluUnit.op2; 
		fprintf(fout, "0x%02x\n", *aluUnit.op1); break;

	case ORL:	
		fprintf(fout, "(0x%02x)ORL		- VAGY m�velet		- fel�l�rt adat: 0x%02x		�j adat: 0x%02x OR 0x%02x = ", PCON, *aluUnit.op1, *aluUnit.op1, aluUnit.op2);
		*aluUnit.op1 |= aluUnit.op2; fprintf(fout, "0x%02x\n", *aluUnit.op1); break;

	case XRL:	
		fprintf(fout, "(0x%02x)XRL		- KIZ�R� VAGY m�velet	- fel�l�rt adat: 0x%02x		�j adat: 0x%02x XOR 0x%02x = ", PCON, *aluUnit.op1, *aluUnit.op1, aluUnit.op2);
		*aluUnit.op1 ^= aluUnit.op2; fprintf(fout, "0x%02x\n", *aluUnit.op1); break;

	case PUSH:
		fprintf(fout, "(0x%02x)PUSH		- adatment�s a stackbe		- fel�l�rt adat: 0x%02x		�j adat: 0x%02x\n", PCON, normalData[SP], *aluUnit.op1);
		normalData[SP] = *aluUnit.op1;
		SP++;
		break;

	case POP:
		SP--;
		fprintf(fout, "(0x%02x)POP		- adatnyer�s a stackb�l		- fel�l�rt adat: 0x%02x		�j adat: 0x%02x\n", PCON, *aluUnit.op1, normalData[SP]);
		*aluUnit.op1 = normalData[SP];
		break;

	case INC:	
		fprintf(fout, "(0x%02x)INC		- inkrement�l�s		- fel�l�rt adat: 0x%02x		�j adat: 0x%02x + 1 = ", PCON, *aluUnit.op1, *aluUnit.op1);
		*aluUnit.op1 += 1; fprintf(fout, "%d\n", *aluUnit.op1); break;

	case CLR: {
		if (seg)
		{
			fprintf(fout, "(0x%02x)CLR		- adatt�rl�s			- fel�l�rt adat: 0x%02x (%d. helyi�rt�k)		�j adat: ", PCON, aluUnit.baOp->data8bit, aluUnit.bit);
			bitClr(aluUnit.bit, aluUnit.baOp);
			fprintf(fout, "0x%02x\n", aluUnit.baOp->data8bit);
			seg = 0;
		}
		else
		{
			fprintf(fout, "(0x%02x)CLR		- adatt�rl�s			- fel�l�rt adat: 0x%02x		�j adat: ", PCON, *aluUnit.op1);
			*aluUnit.op1 = 0; fprintf(fout, "0x%02x\n", *aluUnit.op1);
		}
	} break;

	case CPL: {
		if (seg) {
			fprintf(fout, "(0x%02x)CPL		- komplement�l�s		- fel�l�rt adat: 0x%02x (%d. helyi�rt�k)		�j adat: ", PCON, aluUnit.baOp->data8bit, aluUnit.bit);
			bitCpl(aluUnit.bit, aluUnit.baOp);
			*aluUnit.op1 = ~*aluUnit.op1; fprintf(fout, "0x%02x\n", aluUnit.baOp->data8bit);
			seg = 0;
		}
		else {
			fprintf(fout, "(0x%02x)CPL		- komplement�l�s 		- fel�l�rt adat: 0x%02x		�j adat: ", PCON, *aluUnit.op1);
			*aluUnit.op1 = ~*aluUnit.op1; fprintf(fout, "0x%02x\n", *aluUnit.op1);
		}
	} break;

	case RL:	
		fprintf(fout, "(0x%02x)RL		- balral�ptet�s			- fel�l�rt adat: 0x%02x		�j adat: ", PCON, *aluUnit.op1);
		*aluUnit.op1 = *aluUnit.op1 << 1; fprintf(fout, "0x%02x\n", *aluUnit.op1); break;

	case RLC:	
		fprintf(fout, "(0x%02x)RLC		- balral�ptet�s(CY)		- fel�l�rt adat: 0x%02x		�j adat: ", PCON, *aluUnit.op1);
		res = *aluUnit.op1;
		res = res >> 7;
		res &= 0x0001;
		PSW.d7 = res;
		*aluUnit.op1 = *aluUnit.op1 << 1; fprintf(fout, "0x%02x	CY: %d\n", *aluUnit.op1, PSW.d7); break;

	case RR:	
		fprintf(fout, "(0x%02x)RR		- jobbral�ptet�s 		- fel�l�rt adat: 0x%02x		�j adat: ", PCON, *aluUnit.op1);
		*aluUnit.op1 = *aluUnit.op1 >> 1; fprintf(fout, "0x%02x\n", *aluUnit.op1); break;

	case RRC:	
		fprintf(fout, "(0x%02x)RRC		- jobbral�ptet�s(CY)		- fel�l�rt adat: 0x%02x		�j adat: ", PCON, *aluUnit.op1);
		res = *aluUnit.op1;
		res &= 0x0001;
		PSW.d7 = res;
		*aluUnit.op1 = *aluUnit.op1 >> 1; fprintf(fout, "0x%02x	CY: %d\n", *aluUnit.op1, PSW.d7); break;

	case SWAP:	
		fprintf(fout, "(0x%02x)SWAP		- tetr�dok cser�l�se 		- fel�l�rt adat: 0x%02x		�j adat: ", PCON, *aluUnit.op1);
		res = *aluUnit.op1;
		*aluUnit.op1 = *aluUnit.op1 << 4;
		res = res >> 4;
		*aluUnit.op1 |= res; fprintf(fout, "0x%02x\n", *aluUnit.op1); break;

	case DEC:	
		fprintf(fout, "(0x%02x)DEC		- dekrement�l�s 	- fel�l�rt adat: 0x%02x		�j adat: 0x%02x - 1 = ", PCON, *aluUnit.op1, *aluUnit.op1);
		*aluUnit.op1 -= 1; 
		fprintf(fout, "0x%02x\n", *aluUnit.op1); break;

	case MUL:	
		fprintf(fout, "(0x%02x)MUL		- szorz�s 	- fel�l�rt adat: 0x%02x		�j adat: 0x%02x * 0x%02x  = ", PCON, ACC.data8bit, ACC.data8bit, B.data8bit);
		res = ACC.data8bit * B.data8bit; 
		B.data8bit = res;
		res = res >> 8;
		ACC.data8bit = res;
		PSW.d7 = 0;
		fprintf(fout, "(az eredm�ny 16 bites -> A: 0x%02x, B: 0x%02x)\n", ACC.data8bit, B.data8bit); break;

	case DIV:	
		fprintf(fout, "(0x%02x)DIV		- oszt�s 	- fel�l�rt adat: 0x%02x		�j adat: 0x%02x / 0x%02x = ", PCON, ACC.data8bit, ACC.data8bit, B.data8bit);
		res = ACC.data8bit / B.data8bit;
		B.data8bit = ACC.data8bit % B.data8bit;
		ACC.data8bit = res;
		PSW.d7 = 0;
		fprintf(fout, "(eg�sz sz�m� oszt�s -> A: 0x%02x, B: 0x%02x)\n", ACC.data8bit, B.data8bit); break;

	case SETB:
		fprintf(fout, "(0x%02x)SETB		- bit �ll�t�s			- fel�l�rt adat: 0x%02x		�j adat: ", PCON, aluUnit.baOp->data8bit);
		bitSet(aluUnit.bit, aluUnit.baOp);
		fprintf(fout, "0x%02x\n", aluUnit.baOp->data8bit); break;

	case USING: 
		fprintf(fout, "(0x%02x)USING (regiszterbank v�lt�s: %d)\n", PCON, bankSelect); break;

	default: hiba(s);
	}
	// Utas�t�s v�grehajtva! A programsz�ml�l� a k�vetkez� utas�t�sra l�p - ezt a flag v�ltoz� jelzi
	fprintf(fout, "--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------\n");
	PCON++;
	flag = 1;
}

// kil�p�s a programb�l
void exitProg(param s) {
	int status;
	unsigned int k = 0;
	unsigned int i;
	unsigned int j;
	fprintf(fout, "\nA programst�tusz regiszter tartalma (PSW): 0x%02x\n", PSW.data8bit);
	fprintf(fout, "\n�ltal�nos regiszterek:\n");
	for (i = 0; i < 4u; i++) {
		fprintf(fout, "\nBank%d:\n", i);
		for (j = 0; j < 8u; j++) {
			fprintf(fout, "R%d: 0x%02x\n", j, bank[i].reg[j]);
		}
	}
	i = 0;	j = 0;
	fprintf(fout, "\nSpecial function regiszterek\n");
	fprintf(fout, "a) bitc�mezhet� regiszterek:\n");
	fprintf(fout, "P0:   0x%02x\nP1:   0x%02x\nP2:   0x%02x\nP3:   0x%02x\nTCON: 0x%02x\nSCON: 0x%02x\nIP:   0x%02x\nIE:   0x%02x\nACC:  0x%02x\nB:    0x%02x\nPSW:  0x%02x\nBR0:  0x%02x\nBR1:  0x%02x\nBR2:  0x%02x\nBR3:  0x%02x\nBR4:  0x%02x\n",
		P0.data8bit,
		P1.data8bit,
		P2.data8bit,
		P3.data8bit,
		TCON.data8bit,
		SCON.data8bit,
		IP.data8bit,
		IE.data8bit,
		ACC.data8bit,
		B.data8bit,
		PSW.data8bit,
		baReg[0].data8bit,
		baReg[1].data8bit,
		baReg[2].data8bit,
		baReg[3].data8bit,
		baReg[4].data8bit);
	fprintf(fout, "\nb) nem-bitc�mezhet� regiszterek:\n");
	fprintf(fout, "SP:   0x%02x\nDPL:  0x%02x\nDPH:  0x%02x\nPCON: 0x%02x\nTMOD: 0x%02x\nSBUF: 0x%02x\nTL0:  0x%02x\nTL1:  0x%02x\nTH0:  0x%02x\nTH1:  0x%02x\n",
		SP,
		DPL,
		DPH,
		PCON,
		TMOD,
		SBUF,
		TL0,
		TL1,
		TH0,
		TH1);
	fprintf(fout, "\nA teljes adatmem�ria tartalma (256 byte):\n");
	fprintf(fout, "C�m		Adat\n");
	while (i < 32) {
		fprintf(fout, "%3d		%3d\n", i, *regsRAM[i]);
		i++;
	}
	while (i < 48) {
		fprintf(fout, "%3d		%3d\n", i, baRAM[j]->data8bit);
		i++;
		j++;
	}
	while (i < 128) {
		fprintf(fout, "%3d		%3d\n", i, *normalRAM[k]);
		i++;
		k++;
	}
	while (i < 256) {
		if ((i % 8) == 0) {
			fprintf(fout, "%3d		%3d\n", i, baRAM[j]->data8bit);
			j++;
		}
		else {
			fprintf(fout, "%3d		%3d\n", i, *normalRAM[k]);
			k++;
		}
		i++;
	}
	status = fclose(fout);
	if (status) {
		fprintf(stderr, "Nem sikerult a kimeneti fajlt bezarni!\n");
		exit(EXIT_FAILURE);
	}
}

// m�sodik (immediate) operandus ment�se
void iSvOP2(param s) {
	char buff[10] = "";
	sscanf(s, "#%s", buff);
	if (strcmp(buff, "") != 0) {
		aluUnit.op2 = strtoul(buff, NULL, 0);
		fprintf(fout, "OP2-k�zvetlen: %s		", s);
	}
	else hiba(s);
}

// oszt�s �s szorz�s eset�n csak A �s B regiszterek haszn�lhat�ak
void rAB(param s) {
	in_event reg = scanCode(s);
	if (reg == AB) { fprintf(fout, "OP1-regiszter: A		OP2-regiszter: B		"); }
}

// els� (regiszter) operandus ment�se
void rSvOP1(param s) {
	in_event reg = scanCode(s);
	switch (reg) {
	case R0:	aluUnit.op1 = &bank[bankSelect].reg[0]; break;
	case R1:	aluUnit.op1 = &bank[bankSelect].reg[1]; break;
	case R2:	aluUnit.op1 = &bank[bankSelect].reg[2]; break;
	case R3:	aluUnit.op1 = &bank[bankSelect].reg[3]; break;
	case R4:	aluUnit.op1 = &bank[bankSelect].reg[4]; break;
	case R5:	aluUnit.op1 = &bank[bankSelect].reg[5]; break;
	case R6:	aluUnit.op1 = &bank[bankSelect].reg[6]; break;
	case R7:	aluUnit.op1 = &bank[bankSelect].reg[7]; break;
	case RA:	aluUnit.op1 = &ACC.data8bit; break;
	case RB:	aluUnit.op1 = &B.data8bit; break;
	}
	fprintf(fout, "OP1-regiszter: %s		", s);
}

// m�sodik (regiszter) operandus ment�se
void rSvOP2(param s) {
	in_event reg = scanCode(s);
	switch (reg) {
	case R0: aluUnit.op2 = bank[bankSelect].reg[0]; break;
	case R1: aluUnit.op2 = bank[bankSelect].reg[1]; break;
	case R2: aluUnit.op2 = bank[bankSelect].reg[2]; break;
	case R3: aluUnit.op2 = bank[bankSelect].reg[3]; break;
	case R4: aluUnit.op2 = bank[bankSelect].reg[4]; break;
	case R5: aluUnit.op2 = bank[bankSelect].reg[5]; break;
	case R6: aluUnit.op2 = bank[bankSelect].reg[6]; break;
	case R7: aluUnit.op2 = bank[bankSelect].reg[7]; break;
	case RA: aluUnit.op2 = ACC.data8bit; break;
	case RB: aluUnit.op2 = B.data8bit; break;
	}
	fprintf(fout, "OP2-regiszter: %s		", s);
}

// m�sodik (immediate) operandus ment�se
void irSvOP2(param s) {
	in_event reg = scanCode(s);
	uint8_t idx;
	int i;
	if (reg == R0_I) i = 0;
	else if (reg == R1_I) i = 1;
	else hiba(s);
	idx = bank[bankSelect].reg[i];
	if (getIdx(idx) == 256) hiba(s);
	aluUnit.op2 = getIdx(idx);
	fprintf(fout, "OP2-adat:	0x%02x		", aluUnit.op2);
}

// els� (mem�riac�mmel hivatkozott) operandus ment�se
void aSvOP1(param s) {
	if (seg) hiba(s);
	unsigned long idx = strtoul(s, NULL, 0);
	aluUnit.op1 = getIdxP(idx);
	if (aluUnit.op1 == NULL) hiba(s);
	fprintf(fout, "OP1-adat:	0x%02x		", *aluUnit.op1);
}

// m�sodik (mem�riac�mmel hivatkozott) operandus ment�se
void aSvOP2(param s) {
	if (seg) hiba(s);
	unsigned long idx = strtoul(s, NULL, 0);
	if (getIdx(idx) == 256) hiba(s);
	aluUnit.op2 = getIdx(idx);
	fprintf(fout, "OP2-adat:	0x%02x		", aluUnit.op2);
}

// a m�sodik operandusk�nt megadott c�mke k�dc�m�nek ment�se
void lSvOP2(param s) {
	int i;
	int err = 1;
	for (i = 0; i < lbCnt; i++) {
		if (strcmp(labels[i].lab, s) == 0) {
			aluUnit.op2 = labels[i].addr;
			err = 0;
			break;
		}
	}
	if (err) { hiba(s); }
}

// bitc�m ment�se
void bSvOp(param s) {
	seg = 1;
	unsigned long idx = strtoul(s, NULL, 0);
	aluUnit.baOp = baRAM[bitAddressDecode(idx)];
	aluUnit.bit = idx % 8;
}

// carry flag ment�se
void cSvOp(param s) {
	seg = 1;
	aluUnit.baOp = &PSW;
	aluUnit.bit = 7;
}

// regiszterbank v�laszt�s
void bSelect(param s) {
	unsigned long idx = strtoul(s, NULL, 0);
	bankSelect = idx;
	switch (bankSelect) {
	case 0:	PSW.d3 = 0; PSW.d4 = 0; break;
	case 1:	PSW.d3 = 1; PSW.d4 = 0; break;
	case 2:	PSW.d3 = 0; PSW.d4 = 1; break;
	case 3:	PSW.d3 = 1; PSW.d4 = 1; break;
	default: hiba(s);
	}
}

// a RAM mem�ria val�s�gh� inicializ�l�sa (az indexek megegyeznek az eredeti mem�riac�mekkel)
void initRAM() {
	int i = 0;
	int j = 0;
	for (; i < 16; i++) {
		baRAM[i] = &baData[i];
	}
	for (i = 0; i < 129; i++) {
		normalRAM[i] = &normalData[i];
	}
	normalRAM[132] = &normalData[i]; i++;
	normalRAM[133] = &normalData[i]; i++;
	normalRAM[134] = &normalData[i]; i++;
	normalRAM[136] = &normalData[i]; i++;
	normalRAM[142] = &normalData[i]; i++;
	normalRAM[143] = &normalData[i]; i++;
	normalRAM[144] = &normalData[i]; i++;
	normalRAM[145] = &normalData[i]; i++;
	normalRAM[146] = &normalData[i]; i++;
	normalRAM[147] = &normalData[i]; i++;
	normalRAM[148] = &normalData[i]; i++;
	normalRAM[149] = &normalData[i]; i++;
	normalRAM[150] = &normalData[i]; i++;
	normalRAM[151] = &normalData[i]; i++;
	normalRAM[152] = &normalData[i]; i++;
	j = 154;
	while ( i < 182 ) {
		normalRAM[j] = &normalData[i];
		j++;
		i++;
	}
}

// special function regiszterek elhelyez�se a bitc�mezhet� tartom�nyban
void initSfRegs(void) {
	baRAM[16]      = &P0;
	baRAM[17]      = &TCON;
	baRAM[18]      = &P1;
	baRAM[19]      = &SCON;
	baRAM[20]      = &P2;
	baRAM[21]      = &IE;
	baRAM[22]      = &P3;
	baRAM[23]      = &IP;
	baRAM[24]      = &baReg[0];
	baRAM[25]      = &baReg[1];
	baRAM[26]      = &PSW;
	baRAM[27]      = &baReg[2];
	baRAM[28]      = &ACC;
	baRAM[29]      = &baReg[3];
	baRAM[30]	   = &B;
	baRAM[31]	   = &baReg[4];
	normalRAM[129] = &SP;
	normalRAM[130] = &DPL;
	normalRAM[131] = &DPH;
	normalRAM[135] = &PCON;
	normalRAM[137] = &TMOD;
	normalRAM[138] = &TL0;
	normalRAM[139] = &TL1;
	normalRAM[140] = &TH0;
	normalRAM[141] = &TH1;
	normalRAM[153] = &SBUF;	
}

// regiszterbankok inicializ�l�sa
void initBanks(void) {
	int i = 0;
	int j;
	int k = 0;
	for (; i < 4; i++) {
		for (j = 0; j < 8; j++) {
			regsRAM[k] = &bank[i].reg[j];
			k++;
		}
	}
}

// val�s�gh� mem�rialek�pez�s
void initAddresses() {
	int i = 0;
	int j = 0;
	int k = 0;
	// regiszterek
	while (i < 32) {
		address[i] = regsRAM[i];
		i++;
	}
	// bitc�mezhet� tartom�ny
	while (i < 48) {
		address[i] = baRAM[j];
		i++;
		j++;
	}
	// norm�l mem�ria tartom�ny
	while (i < 128) {
		address[i] = normalRAM[k];
		i++;
		k++;
	}
	// norm�l �s bitc�mezhet� mem�riarekeszek vegyesen
	while (i < 256) {
		if ((i % 8) == 0) {
			address[i] = baRAM[j];
			j++;
		}
		else {
			address[i] = normalRAM[k];
			k++;
		}
		i++;
	}
}

// a bemeneti f�jl olvas�sa �s feldolgoz�sa
int readFile() {
	int i = 0;
	int status;
	char* nl_char;
	fin = fopen("utasitasok.txt", "rt");
	if (fin != NULL) {
		while (!feof(fin)) {
			fgets(codeMemory[i], 40, fin);
			nl_char = strchr(codeMemory[i], '\n');
			if (nl_char != NULL) {
				*nl_char = '\0';
			}
			// a c�mk�k elment�se az �llapotg�p elind�t�sa el�tt
			if (scanCode(codeMemory[i]) == LAB) {
				labels[lbCnt].addr = i;
				labels[lbCnt].lab = codeMemory[i];
				lbCnt++;
			}
			i++;
		}
	}
	else {
		fprintf(stderr,"Nem sikerult a bemeneti fajlt megnyitni!\n");
		exit(EXIT_FAILURE);
	}
	status = fclose(fin);
	if (status) {
		fprintf(stderr, "Nem sikerult a bemeneti fajlt bezarni\n");
		exit(EXIT_FAILURE);
	}
	return i;
}

// az �llapotg�p inicializ�l�sa
void initFSM() {
	in_event i = 0;
	state j = 0;
	for (; j <= END_CODE; j++) {
		for (i = NOP; i <= ERC; i++) {
			ct[j][i].n_state	= ERR;
			ct[j][i].tsk		= hiba;
		}
	}
	for (i = NOP; i <= ERC; i++) {
		ct[ERR][i].n_state		= S;			ct[ERR][i].tsk		= hiba;
	}
	for (i = MOV; i <= XRL; i++) {
		ct[S][i].n_state		= WO1;			ct[S][i].tsk		= unCmd;
	}
	for (i = INC; i <= DEC; i++) {
		ct[S][i].n_state		= WO0;			ct[S][i].tsk		= unCmd;
	}
	for (i = MUL; i <= DIV; i++) {
		ct[S][i].n_state		= WAB;			ct[S][i].tsk		= unCmd;
	}
	for (i = RA; i <= R7; i++) {
		ct[WO0][i].n_state		= EXE;			ct[WO0][i].tsk		= rSvOP1;
		ct[WO1][i].n_state		= WO2;			ct[WO1][i].tsk		= rSvOP1;
		ct[WO2][i].n_state		= EXE;			ct[WO2][i].tsk		= rSvOP2;
	}

	for (i = SJMP; i <= JNC; i++) {
		ct[S][i].n_state		= WL;				ct[S][i].tsk	= unCmd;
	}

	for (i = RL; i <= SWAP; i++) {
		ct[S][i].n_state		= WA;				ct[S][i].tsk	= unCmd;
	}

	for (i = JB; i <= JNB; i++) {
		ct[S][i].n_state		= WbitJ;			ct[S][i].tsk	= unCmd;
	}

	for (i = CLR; i <= CPL; i++) {
		ct[S][i].n_state		= WAC;				ct[S][i].tsk	= unCmd;
	}

		ct[WO2][R0_I].n_state	= EXE;			ct[WO2][R0_I].tsk	= irSvOP2;
		ct[WO2][R1_I].n_state	= EXE;			ct[WO2][R1_I].tsk	= irSvOP2;
		ct[WA][RA].n_state		= EXE;			ct[WA][RA].tsk		= rSvOP1;
		ct[WAB][AB].n_state		= EXE;			ct[WAB][AB].tsk		= rAB;
		ct[WAC][RA].n_state		= EXE;			ct[WAC][RA].tsk		= rSvOP1;
		ct[WAC][C].n_state		= EXE;			ct[WAC][C].tsk		= cSvOp;
		ct[WAC][ADR].n_state	= EXE;			ct[WAC][ADR].tsk	= bSvOp;
		ct[S][USING].n_state	= BS;			ct[S][USING].tsk	= unCmd;
		ct[BS][ADR].n_state		= EXE;			ct[BS][ADR].tsk		= bSelect;
		ct[S][LAB].n_state		= S;			ct[S][LAB].tsk		= saveLab;
		ct[S][DSEG].n_state		= S;			ct[S][DSEG].tsk		= sSelect;
		ct[S][BSEG].n_state		= S;			ct[S][BSEG].tsk		= sSelect;
		ct[S][SETB].n_state		= Wbit;			ct[S][SETB].tsk		= unCmd;
		ct[S][NOP].n_state		= S;			ct[S][NOP].tsk		= nope;
		ct[S][END].n_state		= END_CODE;		ct[S][END].tsk		= exitProg;
		ct[S][RET].n_state		= S;			ct[S][RET].tsk		= retCode;
		ct[Wbit][ADR].n_state	= EXE;			ct[Wbit][ADR].tsk	= bSvOp;
		ct[Wbit][C].n_state		= EXE;			ct[Wbit][C].tsk		= cSvOp;
		ct[WbitJ][ADR].n_state	= WL;			ct[WbitJ][ADR].tsk	= bSvOp;
		ct[WL][LAB].n_state		= EXE;			ct[WL][LAB].tsk		= lSvOP2;
		ct[WO1][ADR].n_state	= WO2;			ct[WO1][ADR].tsk	= aSvOP1;
		ct[WO2][ADR].n_state	= EXE;			ct[WO2][ADR].tsk	= aSvOP2;
		ct[WO2][IMM].n_state	= EXE;			ct[WO2][IMM].tsk	= iSvOP2;
		ct[WO2][LAB].n_state	= EXE;			ct[WO2][LAB].tsk	= lSvOP2;
		ct[WO0][ADR].n_state	= EXE;			ct[WO0][ADR].tsk	= aSvOP1;
		ct[EXE][ENC].n_state	= S;			ct[EXE][ENC].tsk	= exec;
}

// a program inicializ�l�sa �s a kezd��llapotok be�ll�t�sa
void init(int* codeLength) {
	initBanks();
	initSfRegs();
	initRAM();
	initAddresses();
	initialization();
	*codeLength = readFile();
	initFSM();
	PCON = 0;
	flag = 1;
	aluUnit.op1 = 0;
	aluUnit.op2 = 0;
	aluUnit.F = 0;
	fout = fopen("eredmeny.txt", "wt");
	if (fout == NULL) {
		fprintf(stderr, "Nem sikerult a kimeneti fajlt megnyitni!");
		exit(EXIT_FAILURE);
	}
}

int main(void) {
	char* lastPtr = NULL;
	char** strPtr = &lastPtr;
	in_event ev = R0;
	init(&codeLength); 
	elem ve;
	eve_par ep;
	state St = S;
 	while (St < END_CODE) {
		ep = whatHappened(strPtr);
		ve = ct[St][ep.e];
		ve.tsk(ep.str);
		St = ve.n_state;
	}
	exit(EXIT_SUCCESS);
}
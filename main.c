#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>
#include <stdio.h>
#include <stdlib.h>
#include <util/delay.h>
#include "printf_tools.h"

#include "numeros.h"


/***************************************

Configure Connections

 ****************************************/

#ifndef F_CPU
#define F_CPU 16000000ul
#endif

#define C1 PC0
#define C2 PC1
#define C3 PC2
#define C4 PC3
#define C5 PC4

#define	baud 57600  // baud rate
#define baudgen ((F_CPU/(16*baud))-1)  //baud divider

#define SIG 0b01101111
uint16_t EEMEM inicio = 0xFFFF;
uint8_t EEMEM assi = 0xFF;

uint16_t volatile TMP_columns=0;
uint16_t volatile TMP_num=0;

unsigned char volatile RecByte= 10;
unsigned int x=2;
unsigned char piso= 0;
unsigned char chegada= 0;
uint16_t coluna=1;
uint8_t next_state=10;
uint8_t state=10;

#define HC595_PORT   PORTB
#define HC595_DDR    DDRB

#define HC595_DS_POS PC0      //Data pin (DS) pin location
#define HC595_SH_CP_POS PC1      //Shift Clock (SH_CP) pin location
#define HC595_ST_CP_POS PC2      //Store Clock (ST_CP) pin location
//Low level macros to change data (DS)lines
#define HC595DataHigh() (HC595_PORT|=(1<<HC595_DS_POS))
#define HC595DataLow() (HC595_PORT&=(~(1<<HC595_DS_POS)))


#define T0COUNT 256-250

/*********************************************/
/*** *** *** *** HC595 System *** *** *** ***/
/*********************************************/

//Inicializa HC595
void HC595Init()
{
	//Make the Data(DS), Shift clock (SH_CP), Store Clock (ST_CP) lines output
	HC595_DDR|=((1<<HC595_SH_CP_POS)|(1<<HC595_ST_CP_POS)|(1<<HC595_DS_POS));
}
//Clock pulse em SH_CP line
void HC595Pulse()
{
	//Pulse the Shift Clock

	HC595_PORT|=(1<<HC595_SH_CP_POS);//HIGH

	HC595_PORT&=(~(1<<HC595_SH_CP_POS));//LOW

}
// Clock pulse em ST_CP
void HC595Latch()
{
	//Pulse the Store Clock

	HC595_PORT|=(1<<HC595_ST_CP_POS);//HIGH
	_delay_loop_1(1);

	HC595_PORT&=(~(1<<HC595_ST_CP_POS));//LOW
	_delay_loop_1(1);
}
//Escreve para HC595
void HC595Write(uint8_t data)
{
	//Send each 8 bits serially

	//Order is MSB first
	for(uint8_t i=0;i<8;i++)
	{
		//Output the data on DS line according to the
		//Value of MSB
		if(data & 0b10000000)
		{
			//MSB is 1 so output high

			HC595DataLow();

		}
		else
		{
			//MSB is 0 so output high
			HC595DataHigh();

		}

		HC595Pulse();  //Pulse the Clock line
		data=data<<1;  //Now bring next bit at MSB position

	}

	//Now all 16 bits have been transferred to shift register
	//Move them to output latch at one
	HC595Latch();
}


/*********************************************/
/*** *** *** *** Incialização *** *** *** ***/
/*********************************************/

/**** Timers ****/
void cp_init(void) {
	CLKPR = 0x80; //CLKPCE
	CLKPR = 0; //Cp=1
}

void tc0_init(void) {
	TCCR0B = 0; //TC0
	TIFR0 |= (7 << TOV0);
	TCCR0A = 0; //modo normal
	TCNT0 = T0COUNT;
	TIMSK0 = (1 << TOIE0); // com interrupção
	TCCR0B = 3; // Tp=64
}

ISR(TIMER0_OVF_vect) {
 TCNT0 = T0COUNT;
 TMP_columns ++;//incrementa a cada ms
 TMP_num ++;
}

/**** USART ****/

void init_USART(void)
{
  UBRR0 = baudgen;

  UCSR0C = (3<<UCSZ00) // 8 data bits
			| (0<<UPM00) // no parity
			| (0<<USBS0); // 1 stop bit

  UCSR0B = (1<<RXEN0) | (1<<TXEN0) | (1<<RXCIE0); // receber por interrupção e transmitir

  sei();
}

ISR(USART_RX_vect) {

RecByte = UDR0; // guarda ...
RecByte = RecByte-48;

if(RecByte<=9){
printf("Next Stop: %u \n", RecByte);
}
//UDR0 = RecByte; // ... devolve
}

/**** HW e EEPROM ****/
void hw_init(void) {
	DDRC |= 0b00011111;

	sei();
}

void init_eeprom(void) {

	if(SIG != eeprom_read_byte(&assi)) {
		eeprom_write_byte(&assi, SIG);
		eeprom_write_word(&inicio, 0xFFFF); // cria incializa
	}
}

/*********************************************/
/*** *** *** *** Movimentos *** *** *** ***/
/*********************************************/

/*Quando o numero que ao que se pretende chegar é maior esta função indica para o estado
 * stsobe que vai fazer o display apresentar os numeros de forma crescente, aparecendo uma
 * seta a subir entre estes, até ao numéro que se colocou em último.
 *
 * Se o numeo a que se pretende chegar for menor, esta função leva para o estado stdesc que faz
 * um inverso.
 *
 * Se o numero for o mesmo, esta garante que este se mantem nesse estado */


void direcao(void){

	if(RecByte>=0 && RecByte<=10){

		chegada = RecByte;

				if(10==chegada){
					next_state=10;
				}
				else if (chegada<piso){
					next_state=12;  //descer
				}
				else if (chegada==piso){
					next_state= piso; //continua
				}
				else{
					next_state=11; //sobe
				}
	}
		else {
		next_state = piso; //continua
	}
}

/*********************************************/
/*** *** *** *** Estados *** *** *** ***/
/*********************************************/

/* Cada estado representa um display na matriz,
 * sendo que para cada display existe um códgio associado
 * escrito na biblioteca numero.h*/

/* de st0 a st9 todas as funções são identicas, explicado em st0.
 *
 * stsobe e stdesce tamb´me, garantindo a respectiva subida ou descida de estado
 *
 * stinit é o estado inicial que vai verificar em memoria não volatil qual o estado
 * em que se encontra o "elevador" */


void st0(void){

	piso= 0;

		if((TMP_num <= 2000)){

				   if((coluna==1) && (TMP_columns>=1)){
						  coluna++; //incrementa colunas
						  PORTC = ~(1<<C1);; //LIGA coluna 1
						  TMP_columns=0; //coloca variavel a 0 (incrementada em interrupção do clock)
						  HC595Write(zero[0]); //escreve para o HC595
				   }

				   else if((coluna==2) && (TMP_columns>=1)){
						  coluna++;
							  PORTC = ~(1<<C2);; //LIGA coluna 2
						  TMP_columns=0;
							  HC595Write(zero[1]);
				   }

				   else if((coluna==3) && (TMP_columns>=1)){
						  coluna++;
						  PORTC = ~(1<<C3);; //LIGA coluna 3
						  TMP_columns=0;
						  HC595Write(zero[2]);
				   }

				   else if((coluna==4) && (TMP_columns>=1)){
						  coluna++;
						  PORTC = ~(1<<C4);; //LIGA coluna 4
						  TMP_columns=0;
						  HC595Write(zero[3]);

				   }

				   else if((coluna==5) && (TMP_columns>=1)){
						   coluna=1;
						  PORTC = ~(1<<C5);; //LIGA coluna 5
						  TMP_columns=0;
						  HC595Write(zero[4]);
				   }

			}

		else{
				TMP_num=0; //coloca variavel a 0 (incrementada em interrupção do clock)
				direcao(); //calcula qual o estado seguinte
			}

	}
void st1(void){

	piso=1;

		if((TMP_num <= 2000)){

				   if((coluna==1) && (TMP_columns>=1)){
						  coluna++;
						  PORTC = ~(1<<C1);; //LIGA coluna 1
						  TMP_columns=0;
						  HC595Write(um[0]);
				   }

				   else if((coluna==2) && (TMP_columns>=1)){
						  coluna++;
							  PORTC = ~(1<<C2);; //LIGA coluna 2
						  TMP_columns=0;
							  HC595Write(um[1]);
				   }

				   else if((coluna==3) && (TMP_columns>=1)){
						  coluna++;
						  PORTC = ~(1<<C3);; //LIGA coluna 3
						  TMP_columns=0;
						  HC595Write(um[2]);
				   }

				   else if((coluna==4) && (TMP_columns>=1)){
						  coluna++;
						  PORTC = ~(1<<C4);; //LIGA coluna 4
						  TMP_columns=0;
						  HC595Write(um[3]);

				   }

				   else if((coluna==5) && (TMP_columns>=1)){
						   coluna=1;
						  PORTC = ~(1<<C5);; //LIGA coluna 5
						  TMP_columns=0;
						  HC595Write(um[4]);
				   }

			}

		else{
				TMP_num=0;
				direcao();
			}

	}
void st2(void){

	piso=2;

		if((TMP_num <= 2000)){

				   if((coluna==1) && (TMP_columns>=1)){
						  coluna++;
						  PORTC = ~(1<<C1);; //LIGA coluna 1
						  TMP_columns=0;
						  HC595Write(dois[0]);
				   }

				   else if((coluna==2) && (TMP_columns>=1)){
						  coluna++;
							  PORTC = ~(1<<C2);; //LIGA coluna 2
						  TMP_columns=0;
							  HC595Write(dois[1]);
				   }

				   else if((coluna==3) && (TMP_columns>=1)){
						  coluna++;
						  PORTC = ~(1<<C3);; //LIGA coluna 3
						  TMP_columns=0;
						  HC595Write(dois[2]);
				   }

				   else if((coluna==4) && (TMP_columns>=1)){
						  coluna++;
						  PORTC = ~(1<<C4);; //LIGA coluna 4
						  TMP_columns=0;
						  HC595Write(dois[3]);

				   }

				   else if((coluna==5) && (TMP_columns>=1)){
						   coluna=1;
						  PORTC = ~(1<<C5);; //LIGA coluna 5
						  TMP_columns=0;
						  HC595Write(dois[4]);
				   }

			}

		else{
				TMP_num=0;
				direcao();
			}

	}
void st3(void){

	piso= 3;

	if((TMP_num <= 2000)){

			   if((coluna==1) && (TMP_columns>=1)){
					  coluna++;
					  PORTC = ~(1<<C1);; //LIGA coluna 1
					  TMP_columns=0;
					  HC595Write(tres[0]);
			   }

			   else if((coluna==2) && (TMP_columns>=1)){
					  coluna++;
						  PORTC = ~(1<<C2);; //LIGA coluna 2
					  TMP_columns=0;
						  HC595Write(tres[1]);
			   }

			   else if((coluna==3) && (TMP_columns>=1)){
					  coluna++;
					  PORTC = ~(1<<C3);; //LIGA coluna 3
					  TMP_columns=0;
					  HC595Write(tres[2]);
			   }

			   else if((coluna==4) && (TMP_columns>=1)){
					  coluna++;
					  PORTC = ~(1<<C4);; //LIGA coluna 4
					  TMP_columns=0;
					  HC595Write(tres[3]);

			   }

			   else if((coluna==5) && (TMP_columns>=1)){
					   coluna=1;
					  PORTC = ~(1<<C5);; //LIGA coluna 5
					  TMP_columns=0;
					  HC595Write(tres[4]);
			   }

		}

	else{
			TMP_num=0;
			direcao();
		}

	}
void st4(void){

	piso= 4;

	if((TMP_num <= 2000)){

			   if((coluna==1) && (TMP_columns>=1)){
					  coluna++;
					  PORTC = ~(1<<C1);; //LIGA coluna 1
					  TMP_columns=0;
					  HC595Write(quatro[0]);
			   }

			   else if((coluna==2) && (TMP_columns>=1)){
					  coluna++;
						  PORTC = ~(1<<C2);; //LIGA coluna 2
					  TMP_columns=0;
						  HC595Write(quatro[1]);
			   }

			   else if((coluna==3) && (TMP_columns>=1)){
					  coluna++;
					  PORTC = ~(1<<C3);; //LIGA coluna 3
					  TMP_columns=0;
					  HC595Write(quatro[2]);
			   }

			   else if((coluna==4) && (TMP_columns>=1)){
					  coluna++;
					  PORTC = ~(1<<C4);; //LIGA coluna 4
					  TMP_columns=0;
					  HC595Write(quatro[3]);

			   }

			   else if((coluna==5) && (TMP_columns>=1)){
					   coluna=1;
					  PORTC = ~(1<<C5);; //LIGA coluna 5
					  TMP_columns=0;
					  HC595Write(quatro[4]);
			   }

		}

	else{
			TMP_num=0;
			direcao();
		}

}
void st5(void){

	piso=5;

	if((TMP_num <= 2000)){

			   if((coluna==1) && (TMP_columns>=1)){
					  coluna++;
					  PORTC = ~(1<<C1);; //LIGA coluna 1
					  TMP_columns=0;
					  HC595Write(cinco[0]);
			   }

			   else if((coluna==2) && (TMP_columns>=1)){
					  coluna++;
						  PORTC = ~(1<<C2);; //LIGA coluna 2
					  TMP_columns=0;
						  HC595Write(cinco[1]);
			   }

			   else if((coluna==3) && (TMP_columns>=1)){
					  coluna++;
					  PORTC = ~(1<<C3);; //LIGA coluna 3
					  TMP_columns=0;
					  HC595Write(cinco[2]);
			   }

			   else if((coluna==4) && (TMP_columns>=1)){
					  coluna++;
					  PORTC = ~(1<<C4);; //LIGA coluna 4
					  TMP_columns=0;
					  HC595Write(cinco[3]);

			   }

			   else if((coluna==5) && (TMP_columns>=1)){
					   coluna=1;
					  PORTC = ~(1<<C5);; //LIGA coluna 5
					  TMP_columns=0;
					  HC595Write(cinco[4]);
			   }

		}

	else{
			TMP_num=0;
			direcao();
		}

}
void st6(void){

	piso=6;

	if((TMP_num <= 2000)){

			   if((coluna==1) && (TMP_columns>=1)){
					  coluna++;
					  PORTC = ~(1<<C1);; //LIGA coluna 1
					  TMP_columns=0;
					  HC595Write(seis[0]);
			   }

			   else if((coluna==2) && (TMP_columns>=1)){
					  coluna++;
						  PORTC = ~(1<<C2);; //LIGA coluna 2
					  TMP_columns=0;
						  HC595Write(seis[1]);
			   }

			   else if((coluna==3) && (TMP_columns>=1)){
					  coluna++;
					  PORTC = ~(1<<C3);; //LIGA coluna 3
					  TMP_columns=0;
					  HC595Write(seis[2]);
			   }

			   else if((coluna==4) && (TMP_columns>=1)){
					  coluna++;
					  PORTC = ~(1<<C4);; //LIGA coluna 4
					  TMP_columns=0;
					  HC595Write(seis[3]);

			   }

			   else if((coluna==5) && (TMP_columns>=1)){
					   coluna=1;
					  PORTC = ~(1<<C5);; //LIGA coluna 5
					  TMP_columns=0;
					  HC595Write(seis[4]);
			   }

		}

	else{
			TMP_num=0;
			direcao();
		}

}
void st7(void){

	piso=7;

	if((TMP_num <= 2000)){

			   if((coluna==1) && (TMP_columns>=1)){
					  coluna++;
					  PORTC = ~(1<<C1);; //LIGA coluna 1
					  TMP_columns=0;
					  HC595Write(sete[0]);
			   }

			   else if((coluna==2) && (TMP_columns>=1)){
					  coluna++;
						  PORTC = ~(1<<C2);; //LIGA coluna 2
					  TMP_columns=0;
						  HC595Write(sete[1]);
			   }

			   else if((coluna==3) && (TMP_columns>=1)){
					  coluna++;
					  PORTC = ~(1<<C3);; //LIGA coluna 3
					  TMP_columns=0;
					  HC595Write(sete[2]);
			   }

			   else if((coluna==4) && (TMP_columns>=1)){
					  coluna++;
					  PORTC = ~(1<<C4);; //LIGA coluna 4
					  TMP_columns=0;
					  HC595Write(sete[3]);

			   }

			   else if((coluna==5) && (TMP_columns>=1)){
					   coluna=1;
					  PORTC = ~(1<<C5);; //LIGA coluna 5
					  TMP_columns=0;
					  HC595Write(sete[4]);
			   }

		}

	else{
			TMP_num=0;
			direcao();
		}

}
void st8(void){

	piso=8;

	if((TMP_num <= 2000)){

			   if((coluna==1) && (TMP_columns>=1)){
					  coluna++;
					  PORTC = ~(1<<C1);; //LIGA coluna 1
					  TMP_columns=0;
					  HC595Write(oito[0]);
			   }

			   else if((coluna==2) && (TMP_columns>=1)){
					  coluna++;
						  PORTC = ~(1<<C2);; //LIGA coluna 2
					  TMP_columns=0;
						  HC595Write(oito[1]);
			   }

			   else if((coluna==3) && (TMP_columns>=1)){
					  coluna++;
					  PORTC = ~(1<<C3);; //LIGA coluna 3
					  TMP_columns=0;
					  HC595Write(oito[2]);
			   }

			   else if((coluna==4) && (TMP_columns>=1)){
					  coluna++;
					  PORTC = ~(1<<C4);; //LIGA coluna 4
					  TMP_columns=0;
					  HC595Write(oito[3]);

			   }

			   else if((coluna==5) && (TMP_columns>=1)){
					   coluna=1;
					  PORTC = ~(1<<C5);; //LIGA coluna 5
					  TMP_columns=0;
					  HC595Write(oito[4]);
			   }

		}

	else{
			TMP_num=0;
			direcao();
		}

}
void st9(void){

	piso= 9;

	if((TMP_num <= 2000)){

			   if((coluna==1) && (TMP_columns>=1)){
					  coluna++;
					  PORTC = ~(1<<C1);; //LIGA coluna 1
					  TMP_columns=0;
					  HC595Write(nove[0]);
			   }

			   else if((coluna==2) && (TMP_columns>=1)){
					  coluna++;
						  PORTC = ~(1<<C2);; //LIGA coluna 2
					  TMP_columns=0;
						  HC595Write(nove[1]);
			   }

			   else if((coluna==3) && (TMP_columns>=1)){
					  coluna++;
					  PORTC = ~(1<<C3);; //LIGA coluna 3
					  TMP_columns=0;
					  HC595Write(nove[2]);
			   }

			   else if((coluna==4) && (TMP_columns>=1)){
					  coluna++;
					  PORTC = ~(1<<C4);; //LIGA coluna 4
					  TMP_columns=0;
					  HC595Write(nove[3]);

			   }

			   else if((coluna==5) && (TMP_columns>=1)){
					   coluna=1;
					  PORTC = ~(1<<C5);; //LIGA coluna 5
					  TMP_columns=0;
					  HC595Write(nove[4]);
			   }

		}

	else{
			TMP_num=0;
			direcao();
		}

}
void stinit(void){

	if ( (eeprom_read_word(&inicio)<=9) && (eeprom_read_word(&inicio)>=0) ){

			next_state= eeprom_read_word(&inicio); //O estado inicial é o gravado por último
	}
	else {
			next_state=0; //caso seja a primeira vez, o estado incila será 0
	}
}
void stsobe(void){

	if((TMP_num <= 1000)){

			   if((coluna==1) && (TMP_columns>=1)){
					  coluna++;
					  PORTC = ~(1<<C1);; //LIGA coluna 1
					  TMP_columns=0;
					  HC595Write(sobe[0]);
			   }

			   else if((coluna==2) && (TMP_columns>=1)){
					  coluna++;
						  PORTC = ~(1<<C2);; //LIGA coluna 2
					  TMP_columns=0;
						  HC595Write(sobe[1]);
			   }

			   else if((coluna==3) && (TMP_columns>=1)){
					  coluna++;
					  PORTC = ~(1<<C3);; //LIGA coluna 3
					  TMP_columns=0;
					  HC595Write(sobe[2]);
			   }

			   else if((coluna==4) && (TMP_columns>=1)){
					  coluna++;
					  PORTC = ~(1<<C4);; //LIGA coluna 4
					  TMP_columns=0;
					  HC595Write(sobe[3]);

			   }

			   else if((coluna==5) && (TMP_columns>=1)){
					   coluna=1;
					  PORTC = ~(1<<C5);; //LIGA coluna 5
					  TMP_columns=0;
					  HC595Write(sobe[4]);
			   }

		}

	else{
			TMP_num=0;
			next_state = piso + 1; // sobe para o piso seguinte
		}


}
void stdesce(void){

		if((TMP_num <= 1000)){

				   if((coluna==1) && (TMP_columns>=1)){
						  coluna++;
						  PORTC = ~(1<<C1);; //LIGA coluna 1
						  TMP_columns=0;
						  HC595Write(desce[0]);
				   }

				   else if((coluna==2) && (TMP_columns>=1)){
						  coluna++;
							  PORTC = ~(1<<C2);; //LIGA coluna 2
						  TMP_columns=0;
							  HC595Write(desce[1]);
				   }

				   else if((coluna==3) && (TMP_columns>=1)){
						  coluna++;
						  PORTC = ~(1<<C3);; //LIGA coluna 3
						  TMP_columns=0;
						  HC595Write(desce[2]);
				   }

				   else if((coluna==4) && (TMP_columns>=1)){
						  coluna++;
						  PORTC = ~(1<<C4);; //LIGA coluna 4
						  TMP_columns=0;
						  HC595Write(desce[3]);

				   }

				   else if((coluna==5) && (TMP_columns>=1)){
						   coluna=1;
						  PORTC = ~(1<<C5);; //LIGA coluna 5
						  TMP_columns=0;
						  HC595Write(desce[4]);
				   }

			}

		else{
				TMP_num=0;
				next_state = piso - 1; // desce de piso
			}
}

int main()
{

	hw_init();
	HC595Init();
	init_printf_tools();
	tc0_init();
	cp_init();
	init_USART();
	init_eeprom();

	printf("\n *********** Matriz de LEDs *************** \n");
	   while(1)
	   {

			switch(state) {
				case 0: st0(); break;
				case 1: st1(); break;
				case 2: st2(); break;
				case 3: st3(); break;
				case 4: st4(); break;
				case 5: st5(); break;
				case 6: st6(); break;
				case 7: st7(); break;
				case 8: st8(); break;
				case 9: st9(); break;
				case 10: stinit(); break;
				case 11: stsobe(); break;
				case 12: stdesce(); break;
				default: break;

			}
			state=next_state;

			if(piso!=eeprom_read_word(&inicio)){
				eeprom_write_word(&inicio, piso);
			} // grava o piso em que está, só se este não for o que já lá está
	   }

return 0;
}




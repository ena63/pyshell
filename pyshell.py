#!/usr/bin/env python
# -*- coding:utf-8 -*-

# pyshell.py
# Midi Ingenierie. Noe Serres
# version 1: sep 2016. version initiale. fonctionne avec hardware "midi"
# version 2: fev 2017. ajout hardware spécifique client (affectation des GPIO differente)
# version 3: fev 2018. adaptation pour banc de test MIND
# version 4: jul 2018. test avec rack BMAC
# version 5: apr 2024. upload sur GitHub

import serial
import serial.tools.list_ports
import string

#Configuration port serie et baudrate à adapter à l'application
#ser = serial.Serial('/dev/ttyUSB0', 115200,timeout=0.1) #linux 
#ser = serial.Serial('COM12', 115200,timeout=0.3)	#windows
#ser = serial.Serial('COM2', 57600,timeout=0.3)	#windows


def send_command( lacommande ):
	ser.flushInput()	#réinitialise les buffers
	ser.flushOutput()
	lacommande = lacommande.upper() #majuscules
	checksum = 0
	if lacommande[0] == '¤' : #checksum erronée volontairement si commence par ¤ pour test
		lacommande = lacommande[1:]
		checksum = checksum + 1
	for c in lacommande :			#calcul checksum
		checksum = checksum + ord(c)
	#print('checksum=' + str(checksum))
		
		
	lacommande = '{:03}'.format(len(lacommande)) + lacommande + '{:02X}'.format(checksum % 256)#trame Adresse-Commande-Checksum
	lacommandebytes = b"\x02" + bytes(lacommande,'ascii') + b"\x03" #Ajout STX et ETX
	print('sending '+ str(len(lacommandebytes)) + ' bytes :' + str(lacommandebytes))
	ser.write(lacommandebytes)
	reponse = ser.readline()
	print('received ' + str(len(reponse)) + ' bytes :' + str(reponse))

	if reponse.find(b'\x06') == -1: #pas de STX: le module n'acquitte pas la réponse
		return("COM ERROR")
	elif reponse.find(b'\x18') != -1: #pas de XOFFerror: la commande n'a pas été correctement interprétée
		return("SYNTAX ERROR")
	elif reponse.find(b'\x1a') == -1: #pas de XON: il s'agit d'une commande, le module acquitte sans répondre
		return("OK")
	else:
		return(reponse[6:-4].decode('ascii')) #décodage de la réponse

		
if __name__ == '__main__':
	print("****** PYSHELL ******\n")
	
	liste = list(serial.tools.list_ports.grep("0403:60"))	#recherche un port FTDI
	if len(liste)>0 :
		ser = serial.Serial(liste[0].device, 115200, timeout=0.1)
	else:
		print("ERROR: No FTDI serial interface found")
		exit()
	
	while True:
		cmd = input("->>")	#saisir la commande à envoyer à la carte, exemple: 00READ #STATUS

		la_reponse = send_command(cmd)
		print(la_reponse)

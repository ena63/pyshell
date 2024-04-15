#!/usr/bin/env python
# -*- coding:utf-8 -*-

""" -----------------------------------------
	Module de communication DMAC/BMAC
	pour communiquer avec les cartes DMAC/BMAC
	Midi Ingenierie (NS) 2024
	noe.serres@hensoldt.fr
	-----------------------------------------
""" 
__author__ = "Noe Serres"
__version__ = "5.1"

# version 1: sep 2016. version initiale. fonctionne avec hardware "midi"
# version 2: fev 2017. ajout hardware spécifique client (affectation des GPIO differente)
# version 3: fev 2018. adaptation pour banc de test MIND
# version 4: jul 2018. test avec rack BMAC
# version 5: apr 2024. upload sur GitHub https://github.com/ena63/pyshell

import serial # https://github.com/pyserial/pyserial/
import serial.tools.list_ports
import logging

class BMAC:

    STX = '\x02'
    ETX = '\x03'
    
    """
    initialisation et config du port série
    """
    def __init__(self, portCOM=None, baudrate=115200, address=0):
        self.portCOM = portCOM
        self.baudrate = baudrate
        self.address = address
        
        #recherche automatique du port COM FTDI si portCOM=None
        if self.portCOM == None:
            liste = list(serial.tools.list_ports.grep("0403:60"))    # recherche un port FTDI
            if len(liste)>0 :
                logging.info("found FTDI serial port " + liste[0].device)
                self.portCOM = liste[0].device
            else:
                logging.error("ERROR: No FTDI serial interface found")

        
        #instanciation du port série pyserial
        try:
            self.ser = serial.Serial(port=self.portCOM,baudrate=self.baudrate,timeout=0.1)
        except:
            logging.error(f"serial port error")


    """
    envoi d'une commande et gestion de la réponse du module
    """
    def send(self, lacommande):
        self.ser.flushInput()    #réinitialise les buffers
        self.ser.flushOutput()
        lacommande = lacommande.upper() # conversion en majuscules
        if self.address != None:
            lacommande = f"{self.address:02}{lacommande}" # ajout des deux caractères d'adresse
        checksum = sum(ord(c) for c in lacommande) % 256 # calcul de la checksum
        logging.info('checksum=' + str(checksum))

        # Protocole DMAC/BMAC:
        # [STX][SIZ1][SIZ2][SIZ3][ADR1][ADR2][CMD1]...[CMDn][CHK1][CHK2][ETX]
        # https://www.midi-ingenierie.com/documentation/ressources/notes_application/Syntaxe-et-communication-calculateur.pdf
        
        lacommande_str = f"{self.STX}{len(lacommande):03}{lacommande}{checksum % 256:02X}{self.ETX}"
        lacommande_bytes = bytes(lacommande_str,'ascii')
        logging.info('sending '+ str(len(lacommande_bytes)) + ' bytes :' + str(lacommande_bytes))
        
        try:
            self.ser.write(lacommande_bytes)    # envoi sur le port série
        except serial.SerialException as e:
            logging.error('serial error: ' + e)
            return("SERIAL EXCEPTION")
            
        try:    
            reponse = self.ser.readline()       # relecture de la réponse
            logging.info('received ' + str(len(reponse)) + ' bytes :' + str(reponse))
        except serial.SerialException as e:
            logging.error('serial error: ' + e)
            return("SERIAL EXCEPTION")
            

        if reponse.find(b'\x06') == -1: # pas de STX: le module n'acquitte pas la réponse
            return("COM ERROR")
        elif reponse.find(b'\x18') != -1: # pas de XOFFerror: la commande n'a pas été correctement interprétée
            return("SYNTAX ERROR")
        elif reponse.find(b'\x1a') == -1: # pas de XON: il s'agit d'une commande, le module acquitte sans répondre
            return("OK")
        else:
            return(reponse[6:-4].decode('ascii')) # décodage de la réponse (on ignore les caractères de 'protocole')

"""
exemple d'utilisation
"""
if __name__ == '__main__':
    import sys
    
    print("╔════════════════════════════╗")
    print("║           PYSHELL          ║")
    print("║    ©Midi Ingenierie 2024   ║")
    print("╚════════════════════════════╝")

    if sys.version_info < (3, 6):
        print("Erreur : Cette version de Python n'est pas prise en charge. Veuillez utiliser Python 3.6 ou une version ultérieure.")
        sys.exit(1)

    logging.basicConfig(level=logging.ERROR) # logging.ERROR ou logging.INFO
    
    my_bmac = BMAC("COM2",baudrate=115200,address=0)
    
    while True:
        cmd = input("->>")    # saisir la commande à envoyer à la carte, exemple: READ #STATUS
        if cmd=="quit":
            exit()
        else:
            la_reponse = my_bmac.send(cmd)
            print(la_reponse)
            
            

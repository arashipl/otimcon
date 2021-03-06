
# Uses LPR default printer for printing (presumably OTIMCON tickets).
# LPR is used for printing on linux and macOS.
# For Windows check startCommand below.
#
# Usage: ticket_print -pPORT -sSPEED
# eg. python ticket_print.py -p /dev/cu.myPort -s 9600
#
# use python ticker_print.py --help for more info
#

import glob
import getopt

def usage():
	print "Ticket printing for OTIMCON"
	print ""
	print "Usage:"
	print "  ticket_print -pPORT (-sSPEED) -oPORT (-xSPEED) [-i] "
	print "  ticket_print --port=PORT (--speed=SPEED) --printport=PORT (--printspeed=SPEED) [--increment]"
	print "  ticket_print -h | --help"
	print ""
	print "Options:"
	print "  -h --help	   Show this screen"
	print "  -p --port	   Serial port for OTIMCON"
	print "  -s --speed	  Speed of serial port for OTIMCON, default 38400"
	print "  -o --printport  Serial port for thermal printer"	
	print "  -x --printspeed Speed of serial port for thermal printer, default 115200"
	print "  -i --increment  Save every ticket, with incremental filename"
	print ""
	print "Example:"
	print "  ticket_print -p/dev/cu.wchusbserial640 -s9600 -o/dev/cu.wchusbserial641 -i"
	print "  ticket_print --port=COM4 --speed=38400 --printport=COM5"
	print ""
	print "Procedure:"
	print "  1. Connect OTIMCON station, look up the serial port."
	print "  2. Set receipt printer as default printer, or set the --command option."
	print "  3. Start this script. It will automatically convert OTIMCON station to PRINT mode!"
	print "  4. Use the script. Exit with CTRL-C."
	print ""
	print "Copyright (c) Stipe Predanic, given under GNU General Public License v3.0"
	print "Modified to print directly to serial port as supported by some chineese thermal printers - Michal Kus, same license"

def inputParse():
	global startCommand
	try:
		opts, args = getopt.getopt(sys.argv[1:], "hp:s:o:x:i", ["help", "port=", "speed=", "printport=", "printspeed=", "increment"])
	except getopt.GetoptError as err:
		# print help information and exit:
		print str(err)  # will print something like "option -a not recognized"
		usage()
		sys.exit(2)
	port = ''
	speed = '38400'
	pport = ''
	pspeed = '115200'
	increment = False
	for o, a in opts:
		if o in ["-p", "--port"]:
			port = a
		elif o in ("-s", "--speed"):
			speed = a
		elif o in ("-o", "--printport"):
			pport = a
		elif o in ("-x", "--printspeed"):
			pspeed = a
		elif o in ("-i", "--increment"):
			increment = True
		elif o in ("-h", "--help"):
			usage()
			sys.exit()
		else:
			assert False, "unhandled option '" + o +"'"
	print (port)
	return port, speed, pport, pspeed, increment


def convertToPrintMode(ser):
	print "\n!!! NOTICE: Converting the OTIMCON to PRINT mode !!!"
	print "Press CTRL-C in 3 seconds to stop this."
	print ""
	time.sleep(3)
	readData = ser.readline()
	if readData[0:11] == 'Starting...':
		readData = ser.readline()
		print "Connected..."
		if readData[0] == '>':
			ser.flush()
			ser.write(b"SET MODE PRINT\n")
			time.sleep(0.5)
			print "Setting mode PRINT..."
			readData = ser.readline()
			readData = ser.readline()
			if readData[0:10] == 'Mode:PRINT':
				readData = ser.readline() # catch the last ">"
				return True
			else:
				print "!!! Error: OTIMCON refuses PRINT mode!"
		else:
			print "!!! Error: OTIMCON doesn't behave as expected!"
	else:
		print "!!! Error: Cannot connect to OTIMCON. Check port & speed!"
	return False

if __name__ == '__main__':
	import sys, subprocess, serial, time

	port, speed, pport, pspeed, incrementFilename = inputParse()
	if port is '':
		print "\n!!! Error: Port must be defined!"
		print "Use ticket_print --help for usage.\n"
		sys.exit(2)
	if pport is '':
		print "\n!!! Error: Printer port must be defined!"
		print "Use ticket_print --help for usage.\n"
		sys.exit(2)
	try:
		ser = serial.Serial(port, speed, timeout=1)
	except Exception as err:
		print "\n!!! Error: Cannot connect to serial port " + port
		print str(err)
		print ""
		sys.exit(2)

	try:
		printerser = serial.Serial(pport, pspeed, timeout=1)
		print "Printer port opened\n"
	except Exception as err:
		print "\n!!! Error: Cannot connect to serial port COM1 at 115200"
		print str(err)
		print ""
		sys.exit(2)

	print "\nConnected to port " + port + " at " + speed + " baud."
	print "Printing to port " + pport + " at " + pspeed + " baud."

	# convert OTIMCON to PRINT mode
	if convertToPrintMode(ser) is False:
		print "\n!!! Error: Cannot convert OTIMCON to PRINT mode."
		print "OTIMCON state is unknown, check the current OTIMCON mode manually.\n"
		sys.exit(2)

	print ""
	print "Ready, waiting for OTIMCON cards..."

	# everythin OK, loop forever
	ticketNo = 1
	newData = False
	while True:
		time.sleep(0.02)  # sleep added so there is always something is in serial buffer until printing is really needed
		readData = ser.readline()

		# if data is not empty, check if it's the first, if it is - open file, write it, otherwise just append
		if readData is not '':
			if newData is False:
				if incrementFilename is True:
					print "Incrementing filename number.."
					fileName = "temp_ticket_" + str(ticketNo) + ".txt"
				else:
					fileName = "temp_ticket" + ".txt"
				fileOutput = open(fileName, "w")
			newData = True
			sys.stdout.write(readData)
			fileOutput.write(readData)
			printerser.write(readData)
		else: # no new data in buffer, close the temporaty ticket file
			if newData:
				fileOutput.close()
				print "File closed, waiting for next chip" 
				time.sleep(0.1)
				ticketNo += 1
				newData = False

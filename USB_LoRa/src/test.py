import serial
s = serial.Serial('/dev/ttyUSB0', 9600, timeout=15)
print('Dang cho...')
while True:
    data = s.read(20)
    if data:
        print('Received:', data)
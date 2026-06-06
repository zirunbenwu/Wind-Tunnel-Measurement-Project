s = serialport('COM11', 115200, 'Timeout', 2);
configureTerminator(s, "LF");
flush(s);
writeline(s, "0");           % start stream
pause(1);
for i = 1:10
    if s.NumBytesAvailable > 0
        disp(readline(s));
    end
    pause(0.1);
end
clear s
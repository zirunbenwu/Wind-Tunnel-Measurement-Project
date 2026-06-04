function live_view(port)
% LIVE_VIEW  Live plot of TWO HX711 load cells, 1-second averaged.
%   live_view('COM11')
%
% Talks to the streaming-mode Pico firmware (send '0').
% Each streamed line is:  raw0,filt0,raw1,filt1,t_ms
% Only the per-second AVERAGE of each cell's raw value is plotted
% (two separate traces). Close the figure window to stop.

    if nargin < 1, port = 'COM11'; end

    WINDOW_SEC   = 60;     % seconds of history visible
    MAX_POINTS   = 600;    % buffer cap (10 min)
    AVG_PERIOD_S = 0.5;    % seconds per averaged point

    % ---- open serial and request stream ----
    s = serialport(port, 115200, 'Timeout', 2);
    configureTerminator(s, "LF");
    flush(s);

    ack = false;
    for attempt = 1:3
        fprintf("Attempt %d: sent '0', waiting for response...\n", attempt);
        writeline(s, "0");
        t0 = tic;
        while toc(t0) < 2
            if s.NumBytesAvailable > 0
                line = readline(s);
                if contains(line, "STREAM")
                    ack = true; break;
                elseif count(line, ',') == 4
                    ack = true; break;   % already streaming, data seen
                end
            end
        end
        if ack, break; end
    end
    if ~ack
        error("Timed out waiting for data from Pico");
    end
    fprintf("Pico acknowledged stream mode\n");

    % ---- figure ----
    fprintf("Setting up figure...\n");
    fig = figure('Name', 'Load cells (0.5 s average)', 'Visible', 'on');
    ax  = axes(fig); hold(ax, 'on'); grid(ax, 'on');
    h0  = plot(ax, NaN, NaN, '-o', 'DisplayName', 'Cell 1');
    h1  = plot(ax, NaN, NaN, '-s', 'DisplayName', 'Cell 2');
    xlabel(ax, 'time (s)'); ylabel(ax, 'avg raw (counts)');
    title(ax, '0.5-second averaged load-cell data');
    legend(ax, 'Location', 'best');
    drawnow;

    % ---- rolling buffers of averaged points ----
    t_avg  = [];
    a0_avg = [];
    a1_avg = [];

    % current 1-second bucket accumulators
    bucket_sum0 = 0; bucket_sum1 = 0; bucket_n = 0;
    bucket_start = NaN;     % t_ms of first sample in this bucket

    carry = "";             % leftover partial line between chunks
    first_line = true;
    rx_count = 0;
    last_report = tic;

    fprintf("Entering main loop. Close the figure to stop.\n");
    cleanupObj = onCleanup(@() stop_stream(s));

    while isvalid(fig)
        % ---- read all available bytes, split into lines ----
        if s.NumBytesAvailable > 0
            try
                chunk = read(s, s.NumBytesAvailable, 'uint8');
            catch
                break;   % USB hiccup -> exit cleanly
            end
            txt   = carry + string(char(chunk(:)'));
            lines = splitlines(txt);
            carry = lines(end);            % last piece may be incomplete
            lines = lines(1:end-1);

            for k = 1:numel(lines)
                line = strip(lines(k));
                if line == "" || ~(count(line, ',') == 4), continue; end
                vals = sscanf(line, "%f,%f,%f,%f,%f");
                if numel(vals) ~= 5, continue; end

                if first_line
                    fprintf('First line received: "%s"\n', line);
                    first_line = false;
                end

                r0 = vals(1); r1 = vals(3); t_ms = vals(5);
                rx_count = rx_count + 1;

                if isnan(bucket_start), bucket_start = t_ms; end
                bucket_sum0 = bucket_sum0 + r0;
                bucket_sum1 = bucket_sum1 + r1;
                bucket_n    = bucket_n + 1;

                % ---- bucket complete? ----
                if (t_ms - bucket_start) >= AVG_PERIOD_S * 1000
                    t_avg(end+1)  = t_ms / 1000;            %#ok<AGROW>
                    a0_avg(end+1) = bucket_sum0 / bucket_n; %#ok<AGROW>
                    a1_avg(end+1) = bucket_sum1 / bucket_n; %#ok<AGROW>

                    if numel(t_avg) > MAX_POINTS
                        t_avg(1)  = []; a0_avg(1) = []; a1_avg(1) = [];
                    end

                    bucket_sum0 = 0; bucket_sum1 = 0;
                    bucket_n = 0; bucket_start = t_ms;
                end
            end
        end

        % ---- redraw ----
        if ~isempty(t_avg)
            set(h0, 'XData', t_avg, 'YData', a0_avg);
            set(h1, 'XData', t_avg, 'YData', a1_avg);
            tmax = t_avg(end);
            xlim(ax, [max(0, tmax - WINDOW_SEC), tmax + 1]);
            ymin = min([a0_avg a1_avg]); ymax = max([a0_avg a1_avg]);
            if ymax > ymin
                pad = 0.05 * (ymax - ymin);
                ylim(ax, [ymin - pad, ymax + pad]);
            end
        end
        drawnow limitrate;

        % ---- 1 Hz console heartbeat ----
        if toc(last_report) >= 1
            fprintf('  rx %d/s   plotted: %d   bucket n=%d\n', ...
                    rx_count, numel(t_avg), bucket_n);
            rx_count = 0; last_report = tic;
        end
    end
end

function stop_stream(s)
    try
        writeline(s, "x");   % any byte halts streaming on the Pico
    catch
    end
    clear s;
end

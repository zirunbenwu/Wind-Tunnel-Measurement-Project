function live_view(port)

    if nargin < 1
        error('Usage: live_view(''COM7'')');
    end
 
    WINDOW_SEC   = 30;
    MAX_POINTS   = 600;
    AVG_PERIOD_S = 0.2;       % 5 averaged points per second
    BAUD         = 115200;
 
    s = serialport(port, BAUD, 'Timeout', 1);
    configureTerminator(s, "LF");
    cleanupSer = onCleanup(@() delete(s));
    pause(2.0);
    flush(s);
 
    % Handshake.
    gotHandshake = false;
    primedLine   = "";
    for attempt = 1:3
        flush(s);
        writeline(s, "0");
        fprintf('Attempt %d: sent ''0''...\n', attempt);
        tStart = tic;
        while toc(tStart) < 2.0
            if s.NumBytesAvailable == 0
                pause(0.05); continue;
            end
            ln = strtrim(readline(s));
            if ismissing(ln) || ln == "", continue; end
            if ln == "STREAM"
                fprintf('Pico acknowledged stream mode\n');
                gotHandshake = true; break;
            elseif count(ln, ",") == 2
                fprintf('Data flowing: %s\n', ln);
                primedLine = ln;
                gotHandshake = true; break;
            else
                fprintf('  (ignored: %s)\n', ln);
            end
        end
        if gotHandshake, break; end
    end
    if ~gotHandshake
        error('No response from Pico - unplug/replug and retry');
    end
 
    fprintf('Setting up figure...\n');
 
    tBuf    = nan(1, MAX_POINTS);
    rawBuf  = nan(1, MAX_POINTS);
    filtBuf = nan(1, MAX_POINTS);
    nPoints = 0;
    bucketStart = NaN;
    bucketRaw   = 0;
    bucketFilt  = 0;
    bucketN     = 0;
    partial     = "";        % string-typed carryover
 
    fig = figure('Name', 'Live load cell', 'NumberTitle', 'off');
    ax  = axes('Parent', fig);
    hRaw  = plot(ax, NaN, NaN, 'o-', 'LineWidth', 1, 'MarkerSize', 4);
    hold(ax, 'on');
    hFilt = plot(ax, NaN, NaN, 'o-', 'LineWidth', 2, 'MarkerSize', 4);
    hold(ax, 'off');
    grid(ax, 'on');
    xlabel(ax, 'Time (s)');
    ylabel(ax, 'HX711 reading (counts)');
    title(ax, sprintf('Live load cell - %g s averaged', AVG_PERIOD_S));
    legend(ax, {sprintf('Raw (%g s avg)', AVG_PERIOD_S), ...
                sprintf('Filtered (%g s avg)', AVG_PERIOD_S)}, ...
           'Location', 'northwest');
    drawnow;
 
    if primedLine ~= ""
        ingestLine(primedLine);
    end
 
    fprintf('Entering main loop. Close the figure to stop.\n');
 
    lastPrint   = tic;
    samplesRx   = 0;
    diagShown   = false;
 
    while isvalid(fig)
        % --- Read serial chunk ---
        try
            nAvail = s.NumBytesAvailable;
        catch ME
            fprintf('Serial disconnected: %s\n', ME.message);
            break;
        end
 
        if nAvail > 0
            try
                bytes = read(s, nAvail, 'uint8');
            catch ME
                fprintf('Read error: %s\n', ME.message);
                break;
            end
            % Force row, convert to string (handles char-array weirdness).
            chunkStr = string(char(reshape(uint8(bytes), 1, [])));
            combined = partial + chunkStr;
            lines    = splitlines(combined);
 
            if ~diagShown && numel(lines) >= 2
                fprintf('First line received: "%s"\n', lines(1));
                diagShown = true;
            end
 
            if numel(lines) >= 2
                partial = lines(end);                  % maybe-incomplete
                completeLines = lines(1:end-1);
                for k = 1:numel(completeLines)
                    ln = strtrim(completeLines(k));
                    if strlength(ln) == 0, continue; end
                    if ingestLine(ln)
                        samplesRx = samplesRx + 1;
                    end
                end
            else
                partial = lines(1);
            end
        end
 
        % --- Update plot ---
        if nPoints > 0
            set(hRaw,  'XData', tBuf(1:nPoints), 'YData', rawBuf(1:nPoints));
            set(hFilt, 'XData', tBuf(1:nPoints), 'YData', filtBuf(1:nPoints));
 
            tNow = tBuf(nPoints);
            xlo  = max(0, tNow - WINDOW_SEC);
            xhi  = tNow + AVG_PERIOD_S;
            if xhi > xlo, xlim(ax, [xlo xhi]); end
 
            visIdx = tBuf(1:nPoints) >= xlo;
            yvals  = [rawBuf(visIdx), filtBuf(visIdx)];
            yvals  = yvals(~isnan(yvals));
            if ~isempty(yvals)
                lo = min(yvals); hi = max(yvals);
                pad = max(50, (hi - lo) * 0.1);
                ylim(ax, [lo - pad, hi + pad]);
            end
        end
 
        if toc(lastPrint) >= 1.0
            fprintf('  rx %d/s   plotted: %d   bucket n=%d\n', ...
                    samplesRx, nPoints, bucketN);
            samplesRx = 0;
            lastPrint = tic;
        end
 
        drawnow limitrate;
        pause(0.05);
    end
 
    try, write(s, uint8('x'), 'uint8'); pause(0.1); catch, end
 
    % ----- nested helpers -----
    function ok = ingestLine(lnStr)
        ok = false;
        lnChar = char(lnStr);
        if startsWith(lnChar, 'STREAM') || startsWith(lnChar, 'STOP')
            return;
        end
        commas = strsplit(lnChar, ',');
        if numel(commas) ~= 3, return; end
        r = str2double(commas{1});
        f = str2double(commas{2});
        t = str2double(commas{3});
        if any(isnan([r f t])), return; end
        ingest(r, f, t);
        ok = true;
    end
 
    function commitBucket(endMs)
        if bucketN == 0, return; end
        centerS = (bucketStart + endMs) / 2 / 1000;
        if nPoints < MAX_POINTS
            nPoints = nPoints + 1;
        else
            tBuf(1:end-1)    = tBuf(2:end);
            rawBuf(1:end-1)  = rawBuf(2:end);
            filtBuf(1:end-1) = filtBuf(2:end);
        end
        tBuf(nPoints)    = centerS;
        rawBuf(nPoints)  = bucketRaw  / bucketN;
        filtBuf(nPoints) = bucketFilt / bucketN;
        bucketStart = NaN; bucketRaw = 0; bucketFilt = 0; bucketN = 0;
    end
 
    function ingest(rawVal, filtVal, tMs)
        if isnan(bucketStart), bucketStart = tMs; end
        if tMs - bucketStart >= AVG_PERIOD_S * 1000
            commitBucket(tMs);
            bucketStart = tMs;
        end
        bucketRaw  = bucketRaw  + rawVal;
        bucketFilt = bucketFilt + filtVal;
        bucketN    = bucketN    + 1;
    end
end

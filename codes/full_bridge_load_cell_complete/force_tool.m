function force_tool(port)
% FORCE_TOOL  GUI-driven tare + calibration + live force view (IIR-filtered data).
%   force_tool('COM11')
%
% The window opens FIRST, so the STOP button is live at every stage -
% including during taring and calibration. A guided status line walks you
% through each step; you click NEXT to advance. No console prompts.
%
% Flow:
%   For each cell:  TARE (clear cell -> Next) -> enter known grams ->
%                   place weight -> Next -> (computes scale) -> remove -> Next
%   Then: LIVE view. Cells 1 & 2 = down force (red, black) on the left plot;
%         cell 3 = drag on the right plot. SAVE toggles 1 Hz logging.
%
% Uses the FILTERED column from each cell (firmware's IIR output).
% Firmware: streaming mode (send "0"); line = raw0,filt0,...,rawN,filtN,t_ms.
%
% IMPORTANT: have the Pico idle (freshly replugged, not already streaming)
% and the serial monitor closed before running. A single "0" starts the
% stream; the firmware toggles on any byte, so an already-running stream
% would be stopped by our "0".

    if nargin < 1, port = 'COM11'; end

    % ===================== CONFIG =====================
    cfg.NUM_CELLS  = 3;            % 1 today / 3 when all wired
    cfg.BAUD       = 115200;
    cfg.TARE_SEC   = 2.0;
    cfg.CAL_SEC    = 2.0;
    cfg.UNITS      = 'g';         % 'g', 'kg', or 'N'
    cfg.WINDOW_SEC = 30;
    cfg.LOG_PERIOD = 1.0;          % 1 Hz logging
    cfg.G          = 9.81;

    % ---- cell -> plot mapping (0-based INTERNAL; labels shown +1) ----
    if cfg.NUM_CELLS >= 3
        cfg.DOWN_CELLS = [0 1];    % shown as cell 1 (red), cell 2 (black)
        cfg.DRAG_CELLS = 2;        % shown as cell 3 (drag)
    else
        cfg.DOWN_CELLS = 0:(cfg.NUM_CELLS-1);
        cfg.DRAG_CELLS = [];
    end
    % ==================================================
    cfg.NFIELDS = 2*cfg.NUM_CELLS + 1;
    cfg.PORT    = port;

    % units
    switch cfg.UNITS
        case 'N',  cfg.unitK = cfg.G/1000; cfg.ylab = 'Force (N)';
        case 'kg', cfg.unitK = 1/1000;     cfg.ylab = 'Mass (kg)';
        otherwise, cfg.unitK = 1.0;        cfg.ylab = 'Mass (g)';
    end

    buildGUI(cfg);   % everything runs inside the GUI from here
end


% ======================================================================
function buildGUI(cfg)
    S.cfg     = cfg;
    S.s       = [];                 % serialport (opened on Start)
    S.carry   = "";
    S.offset  = nan(1, cfg.NUM_CELLS);
    S.scale   = nan(1, cfg.NUM_CELLS);
    S.curCell = 1;                  % which cell we're calibrating (1-based)
    S.knownG  = nan;
    S.phase   = 'idle';             % idle|tare|askmass|weight|remove|live|done
    S.stop    = false;
    % live buffers
    S.tBuf    = [];
    S.down    = zeros(0, numel(cfg.DOWN_CELLS));
    S.drag    = [];
    S.logging = false; S.logFid = -1; S.logFile = '';
    S.tStart  = []; S.lastLog = [];
    S.retareReq = false;            % set by Re-tare button, handled in live loop

    fig = uifigure('Name','Force tool','Position',[80 80 1000 620]);
    S.fig = fig;

    hasDrag = ~isempty(cfg.DRAG_CELLS);
    if hasDrag
        S.axL = uiaxes(fig,'Position',[50 210 440 360]);
        S.axR = uiaxes(fig,'Position',[520 210 440 360]);
    else
        S.axL = uiaxes(fig,'Position',[80 210 840 360]);
        S.axR = [];
    end
    grid(S.axL,'on'); xlabel(S.axL,'time (s)'); ylabel(S.axL,cfg.ylab);
    title(S.axL,'Down force');
    if hasDrag
        grid(S.axR,'on'); xlabel(S.axR,'time (s)'); ylabel(S.axR,cfg.ylab);
        title(S.axR,'Drag');
    end

    % status text
    S.status = uilabel(fig,'Position',[50 120 900 70],'FontSize',16, ...
        'VerticalAlignment','top', ...
        'Text','Press START to open the stream and begin taring.');

    % readout text (live force)
    S.readout = uilabel(fig,'Position',[50 95 900 24],'FontSize',14, ...
        'FontColor',[0.1 0.1 0.5],'Text','');

    % numeric entry for known mass (hidden until needed)
    S.massField = uieditfield(fig,'numeric','Position',[50 55 120 28], ...
        'Value',200,'Visible','off','Limits',[0 Inf]);
    S.massLabel = uilabel(fig,'Position',[180 55 200 28], ...
        'Text','grams','Visible','off');

    % buttons
    S.btnStart = uibutton(fig,'Position',[50 15 100 32],'Text','Start', ...
        'BackgroundColor',[0.4 0.7 0.95]);
    S.btnNext  = uibutton(fig,'Position',[160 15 100 32],'Text','Next', ...
        'Enable','off');
    S.btnSave  = uibutton(fig,'Position',[270 15 110 32],'Text','Save', ...
        'BackgroundColor',[0.4 0.75 0.4],'Enable','off');
    S.btnRetare = uibutton(fig,'Position',[390 15 110 32],'Text','Re-tare', ...
        'BackgroundColor',[0.95 0.8 0.3],'Enable','off');
    S.btnStop  = uibutton(fig,'Position',[510 15 100 32],'Text','Stop', ...
        'BackgroundColor',[0.85 0.4 0.4]);

    guidata(fig, S);

    S.btnStart.ButtonPushedFcn  = @(b,~) onStart(fig);
    S.btnNext.ButtonPushedFcn   = @(b,~) onNext(fig);
    S.btnSave.ButtonPushedFcn   = @(b,~) onSave(fig);
    S.btnRetare.ButtonPushedFcn = @(b,~) onRetare(fig);
    S.btnStop.ButtonPushedFcn   = @(b,~) onStop(fig);
    fig.CloseRequestFcn         = @(f,~) onStop(f);
end


% ======================================================================
function onStart(fig)
    S = guidata(fig);
    S.btnStart.Enable = 'off';
    S.status.Text = 'Opening stream...';
    drawnow;
    try
        S.s = openStream(S.cfg);
    catch err
        S.status.Text = ['ERROR: ' err.message];
        S.btnStart.Enable = 'on';
        guidata(fig,S); return;
    end
    S.curCell = 1;
    guidata(fig,S);
    beginTare(fig);          % start taring the first cell
end


% ---- begin taring the current cell ----
function beginTare(fig)
    S = guidata(fig);
    ci = S.curCell;
    S.phase = 'tare';
    S.status.Text = sprintf(['CELL %d - TARE\n', ...
        'Make sure NOTHING is on cell %d, then press NEXT to capture zero.'], ci, ci);
    S.btnNext.Enable = 'on';
    S.massField.Visible = 'off'; S.massLabel.Visible = 'off';
    guidata(fig,S);
end


% ---- NEXT button: advances the state machine ----
function onNext(fig)
    S = guidata(fig);
    S.btnNext.Enable = 'off'; drawnow;   % debounce during capture
    ci = S.curCell;
    k  = ci;                              % 1-based == column index

    switch S.phase
        case 'tare'
            S.status.Text = sprintf('CELL %d - capturing zero (%.0f s)...', ci, S.cfg.TARE_SEC);
            drawnow;
            [m, S] = captureAvg(S, S.cfg.TARE_SEC);
            if S.stop, finishStop(fig,S); return; end
            S.offset(k) = m(k);
            % move to mass entry
            S.phase = 'askmass';
            S.massField.Visible = 'on'; S.massLabel.Visible = 'on';
            S.status.Text = sprintf(['CELL %d - tare = %.0f counts.\n', ...
                'Enter the known weight (grams) in the box, then press NEXT.'], ci, S.offset(k));
            S.btnNext.Enable = 'on';

        case 'askmass'
            S.knownG = S.massField.Value;
            if isnan(S.knownG) || S.knownG <= 0
                S.status.Text = 'Enter a positive number of grams, then press NEXT.';
                S.btnNext.Enable = 'on'; guidata(fig,S); return;
            end
            S.massField.Visible = 'off'; S.massLabel.Visible = 'off';
            S.phase = 'weight';
            S.status.Text = sprintf(['CELL %d - place the %g g weight on the cell, ', ...
                'let it settle, then press NEXT.'], ci, S.knownG);
            S.btnNext.Enable = 'on';

        case 'weight'
            S.status.Text = sprintf('CELL %d - settling, then capturing with weight...', ci);
            drawnow;
            [m, S] = captureAvg(S, S.cfg.CAL_SEC, 1.5);   % 1.5 s settle, then median
            if S.stop, finishStop(fig,S); return; end
            S.scale(k) = (m(k) - S.offset(k)) / S.knownG;
            S.phase = 'remove';
            S.status.Text = sprintf(['CELL %d - scale = %.4f counts/g.\n', ...
                'Remove the weight, then press NEXT.'], ci, S.scale(k));
            S.btnNext.Enable = 'on';

        case 'remove'
            if S.curCell < S.cfg.NUM_CELLS
                S.curCell = S.curCell + 1;
                guidata(fig,S);
                beginTare(fig);          % next cell
                return;
            else
                % all cells done -> go live
                guidata(fig,S);
                startLive(fig);
                return;
            end
    end
    guidata(fig,S);
end


% ---- capture a robust steady-state value; honors Stop ----
% Discards an initial settle window, then takes the MEDIAN over the
% averaging window (median rejects the placement transient / 6 Hz ring
% far better than mean, fixing run-to-run scale variation).
function [val, S] = captureAvg(S, secs, settle)
    if nargin < 3, settle = 0; end
    buf = zeros(0, S.cfg.NUM_CELLS);

    % wait up to 4 s for data to start
    tw = tic;
    while toc(tw) < 4
        if S.stop, val = nan(1,S.cfg.NUM_CELLS); return; end
        [rows, S.carry] = drainSerial(S.s, S.carry, S.cfg);
        if ~isempty(rows), break; end
        drawnow;
    end

    % discard a settle window (let the load stabilize after placement)
    if settle > 0
        ts = tic;
        while toc(ts) < settle
            if S.stop, val = nan(1,S.cfg.NUM_CELLS); return; end
            [~, S.carry] = drainSerial(S.s, S.carry, S.cfg);  % drain & toss
            drawnow;
        end
    end

    % collect the averaging window
    t0 = tic;
    while toc(t0) < secs
        if S.stop, break; end
        [rows, S.carry] = drainSerial(S.s, S.carry, S.cfg);
        if ~isempty(rows), buf = [buf; rows]; end %#ok<AGROW>
        drawnow;
    end

    if isempty(buf)
        val = nan(1, S.cfg.NUM_CELLS);
        S.status.Text = 'No data - stream stalled. Press STOP, replug Pico, restart.';
        return;
    end
    val = median(buf, 1);   % median, not mean
end


% ======================================================================
function startLive(fig)
    S = guidata(fig);
    cfg = S.cfg;
    hasDrag = ~isempty(cfg.DRAG_CELLS);
    downCol = cfg.DOWN_CELLS + 1;
    nDown   = numel(downCol);
    downColors = [1 0 0; 0 0 0; 0 0.5 0.85; 0 0.6 0.2];

    fprintf('\n==== Calibration summary ====\n');
    for k = 1:cfg.NUM_CELLS
        fprintf('  cell %d: offset = %10.1f   scale = %.4f counts/g\n', ...
            k, S.offset(k), S.scale(k));
    end

    % build plot lines
    cla(S.axL); hold(S.axL,'on'); grid(S.axL,'on');
    xlabel(S.axL,'time (s)'); ylabel(S.axL,cfg.ylab); title(S.axL,'Down force');
    S.hDown = gobjects(1,nDown);
    for j = 1:nDown
        S.hDown(j) = plot(S.axL, NaN, NaN, '-', 'LineWidth',1.6, ...
            'Color', downColors(min(j,end),:), ...
            'DisplayName', sprintf('cell %d', cfg.DOWN_CELLS(j)+1));
    end
    legend(S.axL,'Location','best');
    S.hDrag = [];
    if hasDrag
        cla(S.axR); hold(S.axR,'on'); grid(S.axR,'on');
        xlabel(S.axR,'time (s)'); ylabel(S.axR,cfg.ylab); title(S.axR,'Drag');
        S.hDrag = plot(S.axR, NaN, NaN, '-', 'LineWidth',1.6, ...
            'Color',[0.85 0.4 0.1], ...
            'DisplayName', sprintf('cell %d', cfg.DRAG_CELLS(1)+1));
        legend(S.axR,'Location','best');
    end

    S.phase = 'live';
    S.btnNext.Enable = 'off';
    S.btnSave.Enable = 'on';
    S.btnRetare.Enable = 'on';
    S.massField.Visible = 'off'; S.massLabel.Visible = 'off';
    S.status.Text = 'LIVE. SAVE toggles 1 Hz logging. RE-TARE re-zeros all cells. STOP ends.';
    S.tBuf = []; S.down = zeros(0,nDown); S.drag = [];
    S.tStart = tic; S.lastLog = tic;
    guidata(fig,S);

    % ---- live loop ----
    while isvalid(fig)
        S = guidata(fig);
        if S.stop, break; end

        % ---- handle a Re-tare request: re-zero all cells in place ----
        if S.retareReq
            S.retareReq = false;
            S.btnRetare.Enable = 'off'; S.btnSave.Enable = 'off';
            S.status.Text = 'RE-TARE: clear ALL cells (motor idle)... capturing in 1 s';
            guidata(fig,S); drawnow; pause(1.0);
            S.status.Text = sprintf('RE-TARE: capturing zero (%.0f s)...', cfg.TARE_SEC);
            guidata(fig,S); drawnow;
            [m, S] = captureAvg(S, cfg.TARE_SEC);
            if S.stop, break; end
            if ~any(isnan(m))
                S.offset = m;       % update zero for every cell; scales unchanged
                fprintf('Re-tared. New offsets:'); fprintf(' %.1f', S.offset); fprintf('\n');
            end
            % reset the visible trace so the plot restarts from the new zero
            S.tBuf = []; S.down = zeros(0,nDown); S.drag = [];
            S.tStart = tic; S.lastLog = tic;
            S.btnRetare.Enable = 'on'; S.btnSave.Enable = 'on';
            S.status.Text = 'LIVE. SAVE toggles 1 Hz logging. RE-TARE re-zeros all cells. STOP ends.';
            guidata(fig,S);
            continue;
        end

        [rows, S.carry] = drainSerial(S.s, S.carry, cfg);
        if ~isempty(rows)
            force = (rows - S.offset) .* (1 ./ S.scale) * cfg.unitK;  % per-cell
            now_t = toc(S.tStart);
            for r = 1:size(force,1)
                S.tBuf(end+1)   = now_t;                %#ok<AGROW>
                S.down(end+1,:) = force(r, downCol);    %#ok<AGROW>
                if hasDrag, S.drag(end+1) = sum(force(r, cfg.DRAG_CELLS+1)); end %#ok<AGROW>
            end
            keep = S.tBuf >= (now_t - cfg.WINDOW_SEC);
            S.tBuf = S.tBuf(keep); S.down = S.down(keep,:);
            if hasDrag, S.drag = S.drag(keep); end

            for j = 1:nDown
                set(S.hDown(j), 'XData', S.tBuf, 'YData', S.down(:,j));
            end
            xlim(S.axL, [max(0, now_t-cfg.WINDOW_SEC), now_t+0.5]);
            if hasDrag
                set(S.hDrag, 'XData', S.tBuf, 'YData', S.drag);
                xlim(S.axR, [max(0, now_t-cfg.WINDOW_SEC), now_t+0.5]);
            end

            txt = 'Live  ';
            for j = 1:nDown
                txt = [txt sprintf('cell%d=%8.3f  ', cfg.DOWN_CELLS(j)+1, S.down(end,j))]; %#ok<AGROW>
            end
            if hasDrag
                txt = [txt sprintf('  drag cell%d=%8.3f %s', cfg.DRAG_CELLS(1)+1, S.drag(end), cfg.UNITS)]; %#ok<AGROW>
            else
                txt = [txt cfg.UNITS]; %#ok<AGROW>
            end
            S.readout.Text = txt;

            if S.logging && toc(S.lastLog) >= cfg.LOG_PERIOD
                fprintf(S.logFid, '%.3f', now_t);
                fprintf(S.logFid, ',%.3f', S.down(end,:));
                if hasDrag, fprintf(S.logFid, ',%.3f', S.drag(end)); end
                fprintf(S.logFid, '\n');
                S.lastLog = tic;
            end
        end
        guidata(fig,S);
        drawnow limitrate;
    end

    finishStop(fig, guidata(fig));
end


% ======================================================================
function onSave(fig)
    S = guidata(fig);
    if ~strcmp(S.phase,'live'), return; end
    cfg = S.cfg; hasDrag = ~isempty(cfg.DRAG_CELLS);
    if ~S.logging
        stamp = datestr(now,'yyyymmdd_HHMMSS'); %#ok<TNOW1,DATST>
        S.logFile = sprintf('force_log_%s.csv', stamp);
        S.logFid  = fopen(S.logFile,'w');
        hdr = 't_s';
        for j = 1:numel(cfg.DOWN_CELLS)
            hdr = [hdr sprintf(',cell%d_%s', cfg.DOWN_CELLS(j)+1, cfg.UNITS)]; %#ok<AGROW>
        end
        if hasDrag, hdr = [hdr sprintf(',drag_cell%d_%s', cfg.DRAG_CELLS(1)+1, cfg.UNITS)]; end
        fprintf(S.logFid, '%s\n', hdr);
        S.lastLog = tic; S.logging = true;
        S.btnSave.Text = 'Logging...'; S.btnSave.BackgroundColor = [0.85 0.4 0.4];
        fprintf('Logging -> %s\n', S.logFile);
    else
        S.logging = false;
        if S.logFid > 0, fclose(S.logFid); S.logFid = -1; end
        S.btnSave.Text = 'Save'; S.btnSave.BackgroundColor = [0.4 0.75 0.4];
        fprintf('Logging stopped: %s\n', S.logFile);
    end
    guidata(fig,S);
end


function onRetare(fig)
    S = guidata(fig);
    if ~strcmp(S.phase,'live'), return; end
    S.retareReq = true;          % live loop picks this up and re-zeros
    guidata(fig,S);
end


function onStop(fig)
    S = guidata(fig);
    S.stop = true;
    guidata(fig,S);
    % if we're sitting idle at a prompt (not inside a loop), close now
    if any(strcmp(S.phase, {'idle','tare','askmass','weight','remove','done'}))
        finishStop(fig, S);
    end
end


function finishStop(fig, S)
    if S.logFid > 0, fclose(S.logFid); S.logFid = -1; end
    if ~isempty(S.s)
        try
            writeline(S.s, "x");   % stop the stream
        catch
        end
        try
            delete(S.s);
        catch
        end
        S.s = [];
    end
    if isvalid(fig)
        try
            guidata(fig,S);
        catch
        end
        delete(fig);
    end
    fprintf('Stopped.\n');
end


% ======================================================================
function s = openStream(cfg)
    s = serialport(cfg.PORT, cfg.BAUD, 'Timeout', 2);
    configureTerminator(s, "LF");
    flush(s);
    pause(0.2);
    if s.NumBytesAvailable > 0
        read(s, s.NumBytesAvailable, 'uint8');   % drain boot/leftover bytes
    end

    % Firmware toggles stream on ANY byte. From an idle (just-replugged)
    % Pico, a single "0" starts streaming. Send exactly one "0" and wait
    % for a well-formed data line. No extra bytes (they'd toggle it off).
    flush(s);
    writeline(s, "0");
    carry = ""; ack = false; t0 = tic;
    while toc(t0) < 4
        [rows, carry] = drainSerial(s, carry, cfg); %#ok<ASGLU>
        if ~isempty(rows), ack = true; break; end
    end
    if ~ack
        delete(s);
        error(['No well-formed data after sending "0". The Pico was likely already ', ...
               'streaming so "0" stopped it. Unplug/replug the Pico (boots idle), ', ...
               'close the serial monitor, then press Start again. ', ...
               'Also confirm NUM_CELLS (%d) matches the firmware.'], cfg.NUM_CELLS);
    end
    fprintf('Streaming.\n');
end


% ---- read available bytes -> complete rows of FILTERED values ----
function [rows, carry] = drainSerial(s, carry, cfg)
    rows = zeros(0, cfg.NUM_CELLS);
    if s.NumBytesAvailable == 0, return; end
    try
        chunk = read(s, s.NumBytesAvailable, 'uint8');
    catch
        return;
    end
    txt   = carry + string(char(chunk(:)'));
    lns   = splitlines(txt);
    carry = lns(end);
    lns   = lns(1:end-1);
    for i = 1:numel(lns)
        ln = strip(lns(i));
        if ln == "" || count(ln, ',') ~= (cfg.NFIELDS-1), continue; end
        v = sscanf(ln, '%f,');
        if numel(v) ~= cfg.NFIELDS, continue; end
        vals = zeros(1, cfg.NUM_CELLS);
        for c = 1:cfg.NUM_CELLS
            vals(c) = v(2*c);       % FILTERED (IIR) value of cell c (field index 2c)
        end
        rows(end+1, :) = vals;    %#ok<AGROW>
    end
end
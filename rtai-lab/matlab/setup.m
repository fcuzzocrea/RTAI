% *******
% setup.m
% *******
%
% Don't forget to copy ptinfo.tlc to [matlabroot]\rtw\c\tlc\mw if you're
% using Matlab 7.0 or newer.

devices = [matlabroot, '/rtw/c/rtai/devices'];
addpath(devices);
% devices = [matlabroot, '/rtw/c/rtai/lib'];
% addpath(devices);
savepath;

cd devices
sfuns = dir('*.c')
for cnt = 1:length(sfuns)
    eval(['mex ' sfuns(cnt).name])
end

% ****************

clear all; clc;

input_multislice = multem_default_values();         % Load default values;

input_multislice.precision = 1;                     % eP_Float = 1, eP_double = 2
input_multislice.device = 2;                        % eD_CPU = 1, eD_GPU = 2
input_multislice.cpu_ncores = 1; 
input_multislice.cpu_nthread = 4; 
input_multislice.gpu_device = 0;
input_multislice.gpu_nstream = 8;

input_multislice.E_0 = 300;                          % Acceleration Voltage (keV)
input_multislice.theta = 0.0;
input_multislice.phi = 0.0;

input_multislice.lx = 20;
input_multislice.ly = 20;

input_multislice.nx = 1024; 
input_multislice.ny = 1024;

%%%%%%%%%%%%%%%%%%%%%%%%%%% Incident wave %%%%%%%%%%%%%%%%%%%%%%%%%%
input_multislice.iw_type = 2;   % 1: Plane_Wave, 2: Convergent_wave, 3:User_Define, 4: auto
input_multislice.iw_psi = read_psi_0_multem(input_multislice.nx, input_multislice.ny);    % user define incident wave
input_multislice.iw_x = 0.5*input_multislice.lx;    % x position 
input_multislice.iw_y = 0.5*input_multislice.ly;    % y position

%%%%%%%%%%%%%%%%%%%%%%%% condenser lens %%%%%%%%%%%%%%%%%%%%%%%%
input_multislice.cond_lens_m = 0;                  % Momentum of the vortex
input_multislice.cond_lens_f = 20.4346;             % Defocus (�)
input_multislice.cond_lens_Cs3 = 1e-03;            % Third order spherical aberration (mm)
input_multislice.cond_lens_Cs5 = 0.00;             % Fifth order spherical aberration (mm)
input_multislice.cond_lens_mfa2 = 0.0;             % Twofold astigmatism (�)
input_multislice.cond_lens_afa2 = 0.0;             % Azimuthal angle of the twofold astigmatism (�)
input_multislice.cond_lens_mfa3 = 0.0;             % Threefold astigmatism (�)
input_multislice.cond_lens_afa3 = 0.0;             % Azimuthal angle of the threefold astigmatism (�)
input_multislice.cond_lens_inner_aper_ang = 0.0;   % Inner aperture (mrad) 
input_multislice.cond_lens_outer_aper_ang = 24.0;  % Outer aperture (mrad)
input_multislice.cond_lens_sf = 32;                % Defocus Spread (�)
input_multislice.cond_lens_nsf = 10;               % Number of integration steps for the defocus Spread
input_multislice.cond_lens_beta = 0.2;             % Divergence semi-angle (mrad)
input_multislice.cond_lens_nbeta = 10;             % Number of integration steps for the divergence semi-angle
input_multislice.cond_lens_zero_defocus_type = 2;  % eZDT_First = 1, eZDT_User_Define = 2
input_multislice.cond_lens_zero_defocus_plane = 95;

for x = (0.4:0.025:0.6)*input_multislice.lx
    for y = (0.4:0.025:0.6)*input_multislice.ly
        
        input_multislice.iw_x = x;
        input_multislice.iw_y = y;

        tic;
        output_incident_wave = il_incident_wave(input_multislice); 
        toc;
        psi_0 = flipud(output_incident_wave.psi_0);
        figure(2);
        subplot(1, 2, 1);
        imagesc(abs(psi_0).^2);
        title('intensity');
        axis image;
        colormap gray;

        subplot(1, 2, 2);
        imagesc(angle(psi_0));
        title('phase');
        axis image;
        colormap gray;
        pause(0.2);
    end;
end;
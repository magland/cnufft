%function test_cnufftspread
% Magland, edited Barnett

%compile_mex_cnufftspread; % if needed

if 1
    N=100;
    M=1e6;
    kx=rand(M,1)*(N-1);   % grid pts go from 0 to N-1
    ky=rand(M,1)*(N-1);
    kz=rand(M,1)*(N-1);
  %  kx=(kx-N/2)*0.9+N/2;  % pull in from box edges
  %  ky=(ky-N/2)*0.9+N/2;
  %  kz=(kz-N/2)*0.9+N/2;
    %kz=linspace(0,N,M)';
    
    X=ones(M,1); % randn(M,1); % strengths
    nspread=8; kernel_params=[1;nspread;1;1];         % 2.3 s, 1 core
    %nspread = 16; kernel_params=[1;nspread;0.94;1.46]; % 18 s, 1 core
else
  wrap = 1;   % 0: center pt of box, 1: wraps to center pt of box
    N=20;
    M=1;
    kx=N/2 + N*wrap;
    ky=N/2 + N*wrap;
    kz=N/2 + N*wrap;
    X=1;
    nspread=8;
    kernel_params=[1;nspread;1;1];
end;

tic;
Y=cnufftspread_type1(N,kx,ky,kz,X,kernel_params);
toc

figure;
subplot(1,3,1); imagesc(Y(N/2,:,:)); title('x=N/2');
subplot(1,3,2); imagesc(Y(:,N/2,:)); title('y=N/2');
subplot(1,3,3); imagesc(Y(:,:,N/2)); title('z=N/2');



if 0, figure; sc = max(abs(Y(:))); % anim slices
for k=1:N,imagesc(Y(:,:,k));title(sprintf('k=%d',k))
  caxis([-1 1]*sc); colorbar; drawnow; pause(0.01); end
end

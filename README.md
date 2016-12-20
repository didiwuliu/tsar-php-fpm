tsar module for php-fpm
=======================

1. Install tsar && tsardevel

        $ git clone git://github.com/kongjian/tsar.git
        $ cd tsar
        $ make
        $ sudo make install
        $ sudo make tsardevel

1. Install tsar-php-fpm

        $ git clone https://github.com/guoxiaod/tsar-php-fpm.git
        $ cd tsar-php-fpm 
        $ make
        $ sudo make install

1. Config php-fpm && nginx

        $ sudo vim /etc/php-fpm.d/www.conf

        # make sure /etc/php-fpm.d/www.conf has follow config:
        listen = 9000;
        pm.status_path = /php-fpm-status

        $ sudo vim /etc/nginx/conf.d/status.conf
    
        server {
            server_name 127.0.0.1;
            root /var/www/html;
            location /php-fpm-status {
                allow 127.0.0.1;
                deny all;
                include        fastcgi_params;
                fastcgi_pass   127.0.0.1:9000;
                fastcgi_param  SCRIPT_FILENAME  $fastcgi_script_name;
            }
        }

1. If your nginx has self define port or server_name, export these variables before run tsar

        export PHP_FPM_TSAR_HOST=127.0.0.1                  # default is 127.0.0.1
        export PHP_FPM_TSAR_PORT=80                         # default is 80
        export PHP_FPM_TSAR_URI=/php-fpm-status             # default is /php-fpm-status
        export PHP_FPM_TSAR_SERVER_NAME=status.example.com  # default is 127.0.0.1
        
    tsar-php-fpm will get the php-fpm status like this:
    
        curl -H 'Host: status.example.com' 'http://127.0.0.1:80/php-fpm-status'

1. restart nginx && php-fpm

        $ sudo /etc/init.d/php-fpm restart
        $ sudo /etc/init.d/nginx restart 
    
        or 
    
        $ sudo systemctl restart php-fpm
        $ sudo systemctl restart nginx
    
1. use tsar:
  
        $ tsar --php-fpm -i 1 -l
        Time              -----------------------------------------php-fpm---------------------------------------- 
        Time              accept   queue    maxq    qlen    idle  active   total  maxact  maxrea     qps    sreq   
        20/12/16-12:52:53   1.00    0.00    6.00  128.00    9.00    1.00   10.00   21.00    0.00    1.00    0.00   
        20/12/16-12:52:54   1.00    0.00    6.00  128.00    9.00    1.00   10.00   21.00    0.00    1.00    0.00   
        20/12/16-12:52:55   1.00    0.00    6.00  128.00    9.00    1.00   10.00   21.00    0.00    1.00    0.00   

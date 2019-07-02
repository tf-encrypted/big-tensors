FROM tensorflow/tensorflow:custom-op

RUN wget https://gmplib.org/download/gmp/gmp-6.1.2.tar.xz
RUN tar -xf gmp-6.1.2.tar.xz
RUN cd gmp-6.1.2 && ./configure --with-pic --enable-cxx --enable-static --disable-shared && make && make check && make install

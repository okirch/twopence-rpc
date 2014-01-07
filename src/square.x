struct square_in {
       long arg1;
};

struct square_out {
       long res1;
};

struct foodata {
	unsigned int buffer<>;
};

program SQUARE_PROG {
	version SQUARE_VERS {
		square_out SQUAREPROC(square_in) = 1;
		void ERRNOPROG(void) = 2;
		void ERRPROGVERS(void) = 3;
		void ERRNOPROC(void) = 4;
		void ERRDECODE(void) = 5;
		void ERRSYSTEMERR(void) = 6;
		void ERRWEAKAUTH(void) = 7;

		void SINKPROC(foodata) = 10;
		unsigned int SUMPROC(foodata) = 11;
	} = 1;
} = 202020;

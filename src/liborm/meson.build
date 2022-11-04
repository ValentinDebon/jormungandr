liborm = library('orm',
	include_directories : headers,
	install : true,
	c_args : '-D_GNU_SOURCE',
	sources : 'sandbox.c'
)

pkgconfig.generate(liborm, description : 'Ouroboros jormungandr helper library.')

use base "basetest";
use bmwqemu;

sub run()
{
	my $self=shift;
	x11_start_program("firefox");
	$self->take_screenshot;
	if($ENV{DESKTOP} eq "xfce") {
		sendkey "alt-f4"; # default browser setting popup
		waitidle;
	}
	$self->take_screenshot;
	sendkey "alt-f4"; sleep 2;
	sendkey "ret"; # confirm "save&quit"
}

sub checklist()
{
	# return hashref:
	# firefox disabled icons (back, forw, stop) differ by one bit between 32/64 arch
	return {qw(
		fc382651f1e1b6359789038ad0bd9bc0 OK
		4299006210d21ee52570d99916500f76 OK
	)}
}

1;
# Documentation: https://docs.brew.sh/Formula-Cookbook
#                https://rubydoc.brew.sh/Formula
class Pwol < Formula
  desc "A tool to send Wake-on-LAN packets to wake-up computers"
  homepage "https://github.com/ptrrkssn/pwol"
  url "https://github.com/ptrrkssn/pwol/archive/v1.5.2.tar.gz"

  ## This line must be uncommented and updated with the correct hash value.
  ## The correct value will be displayed when doing 'brew install pwol'
  # sha256 "f75e6c66d31eb147ea5cf319d851705fdd6d68d7ac25b17f942f4b37a7150fe3"
  
  def install
    system "./configure", "--prefix=#{prefix}"
    system "make install"
  end

  test do
    system "#{bin}/pwol", "-v", "00:01:02:03:04:05"
  end
end

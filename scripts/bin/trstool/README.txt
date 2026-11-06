打包说明：
trstool
	$ lsb_release -a; python3 --version
	No LSB modules are available.
	Distributor ID:	Ubuntu
	Description:	Ubuntu 16.04.7 LTS
	Release:	16.04
	Codename:	xenial
	Python 3.6.13
trstool.exe
	Windows10
	Python 3.7.9

//生成pem格式私钥
./secure_tool generate_signing_key key.pem    

//从私钥生成公钥
./secure_tool extract_public_key --keyfile key.pem pub.key    

//用公钥验证已经签名的文件
./secure_tool verify_signature --keyfile pub.key 1_sign.bin    
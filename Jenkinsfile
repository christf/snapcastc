node {
  def RELEASETYPE
    def WORKSPACE
    def PROJECTDIR
    def INCREMENTCOMMAND
    def DATE
    def GIT_COMMIT

    // TODO can we generate the version from ${env.BUILD_NUMBER} and still pass debian linters
    stage('Preparation') { // for display purposes
      git 'https://github.com/christf/snapcastc.git'
      if (params.DEBUG) {
        RELEASETYPE = "Debug"
	DATE = sh (
	  script: 'date',
	  returnStdout: true
	).trim()
	GIT_COMMIT = sh (
	  script: 'git log -n 1 --pretty=format:"%h"',
	  returnStdout: true
	).trim()
	INCREMENTCOMMAND = " && dch -l ${env.BUILD_NUMBER} \"local Build at ${DATE} for commit ${GIT_COMMIT}\""
      }
      else {
	RELEASETYPE = "Release"
      }

      sh "docker build -t snapcastc-build ."
      sh "pwd > workspace"
      WORKSPACE = readFile('workspace').trim()
      PROJECTDIR = "snapcastc"
      sh "rm -rf deb; mkdir deb"
  }
  stage('Build') {
    if (isUnix()) {
      sh "docker run -v $WORKSPACE:/$PROJECTDIR -a STDIN -a STDOUT -a STDERR snapcastc-build /bin/sh -c 'mkdir -p /$PROJECTDIR/build && cd /$PROJECTDIR/build && cmake -DCMAKE_BUILD_TYPE=$RELEASETYPE .. && make -j5'"
    }
  }
  stage('Test') {
    sh "docker run -v $WORKSPACE:/$PROJECTDIR -a STDIN -a STDOUT -a STDERR snapcastc-build /$PROJECTDIR/build/src/snapcast-test > testlog 2>&1"
  }
  stage('Package') {
    parallel (
     'PackageX86': {
	sh "mkdir -p deb/debian"
	sh "docker run -v $WORKSPACE:/$PROJECTDIR -a STDIN -a STDOUT -a STDERR snapcastc-build /bin/sh -c 'cd /$PROJECTDIR/build $INCREMENTCOMMAND && ../scripts/make_debian_package && mv /*.deb /$PROJECTDIR/build/'"
	sh "cp build/*.deb deb/debian"
	sh "docker run -v $WORKSPACE:/$PROJECTDIR -a STDIN -a STDOUT -a STDERR snapcastc-build /bin/sh -c 'rm -rf /$PROJECTDIR/build/'"
      },
      'PackageARM': {
	sh "mkdir -p deb/raspbian"
	sh "ssh pi@raspbmc 'rm -rf /tmp/snapcastc; cd /tmp; git clone https://github.com/christf/snapcastc.git; mkdir snapcastc/build; cd snapcastc/build $INCREMENTCOMMAND &&../scripts/make_debian_package'"
	sh "scp 'pi@raspbmc:/tmp/snapcastc_*.deb' deb/raspbian; ssh pi@raspbmc 'rm -rf /tmp/snapcastc*'"
      }
    )
  }
  
  stage('Upload') {
    sh "package_cloud push christf/dev/debian/stretch deb/debian/*.deb"
    sh "package_cloud push christf/dev/raspbian/stretch deb/raspbian/*.deb"
  }
  stage('Results') {
    archiveArtifacts 'deb/*/*.deb'
    archiveArtifacts 'testlog'
  }
  stage("Cleanup") {
    sh "rm -f testlog"
    sh "docker run -v $WORKSPACE:/$PROJECTDIR -a STDIN -a STDOUT -a STDERR snapcastc-build rm -rf /$PROJECTDIR/build"
  }
}

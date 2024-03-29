node {
  def RELEASETYPE
    def WORKSPACE
    def PROJECTDIR
    def INCREMENTCOMMAND
    def DATE
    def GIT_COMMIT

    // TODO can we generate the version from ${env.BUILD_NUMBER} and still pass debian linters
    stage('Preparation') { // for display purposes
      git branch: env.gitbranch, url: 'https://github.com/christf/snapcastc.git'
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

      sh "./scripts/builddockerfile"
      sh "docker build -t snapcastc-build ."
      sh "pwd > workspace"
      WORKSPACE = readFile('workspace').trim()
      PROJECTDIR = "snapcastc"
      sh "rm -rf deb build; mkdir deb"
  }
  stage('Build') {
    if (isUnix()) {
      sh "docker run -u `id -u $USER` -v $WORKSPACE:/$PROJECTDIR -a STDIN -a STDOUT -a STDERR snapcastc-build /bin/sh -c 'rm -rf /$PROJECTDIR/build; mkdir -p /$PROJECTDIR/build && cd /$PROJECTDIR/build && cmake -DCMAKE_BUILD_TYPE=$RELEASETYPE .. && make -j5'"
    }
  }
  stage('Test') {
	parallel (
		'Test Client': {
			sh "docker run -v $WORKSPACE:/$PROJECTDIR -a STDIN -a STDOUT -a STDERR snapcastc-build /$PROJECTDIR/build/src/snapcast-test-client > testlog-client 2>&1"
			sh "cat testlog-client"
		},
		'Test Server': {
			sh "docker run -v $WORKSPACE:/$PROJECTDIR -a STDIN -a STDOUT -a STDERR snapcastc-build /$PROJECTDIR/build/src/snapcast-test-srv > testlog-srv 2>&1"
			sh "cat testlog-srv"
		}
	)
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
	sh "ssh pi@wohnzimmer 'rm -rf /tmp/snapcastc; cd /tmp; git clone https://github.com/christf/snapcastc.git; mkdir snapcastc/build; cd snapcastc/build $INCREMENTCOMMAND &&../scripts/make_debian_package'"
	sh "scp 'pi@wohnzimmer:/tmp/snapcastc_*.deb' deb/raspbian; ssh pi@wohnzimmer 'rm -rf /tmp/snapcastc*'"
      }
    )
  }
  
  stage('Upload') {
    sh "/usr/local/bin/package_cloud push christf/dev/debian/bullseye deb/debian/*.deb"
    sh "/usr/local/bin/package_cloud push christf/dev/raspbian/bullseye deb/raspbian/*.deb"
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

<script lang="ts">
	import { onMount } from 'svelte';
	import * as THREE from 'three';
	import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';
	import { OrbitControls } from 'three/addons/controls/OrbitControls.js';

	interface Props {
		glbUrl: string;
		status: 'pending' | 'processing' | 'ready' | 'error';
	}

	let { glbUrl, status }: Props = $props();

	let container: HTMLDivElement;
	let renderer: THREE.WebGLRenderer | null = null;
	let scene: THREE.Scene;
	let camera: THREE.PerspectiveCamera;
	let controls: OrbitControls;
	let animId: number;
	let modelLoaded = $state(false);

	function fitCameraToObject(object: THREE.Object3D) {
		const box = new THREE.Box3().setFromObject(object);
		const center = box.getCenter(new THREE.Vector3());
		const size = box.getSize(new THREE.Vector3());
		const maxDim = Math.max(size.x, size.y, size.z);
		const fov = camera.fov * (Math.PI / 180);
		const distance = Math.abs(maxDim / (2 * Math.tan(fov / 2))) * 1.5;

		camera.near = distance * 0.01;
		camera.far = distance * 100;
		camera.updateProjectionMatrix();

		camera.position.set(center.x + distance, center.y + distance * 0.5, center.z + distance);
		camera.lookAt(center);
		controls.target.copy(center);
		controls.update();
	}

	async function loadModel() {
		if (!renderer) return;
		const loader = new GLTFLoader();
		try {
			const gltf = await new Promise<any>((resolve, reject) => {
				loader.load(glbUrl, resolve, undefined, reject);
			});
			scene.add(gltf.scene);
			fitCameraToObject(gltf.scene);
			modelLoaded = true;
		} catch (err) {
			console.error('Failed to load GLB:', err);
		}
	}

	function animate() {
		animId = requestAnimationFrame(animate);
		controls.update();
		renderer!.render(scene, camera);
	}

	function handleResize(entries: ResizeObserverEntry[]) {
		for (const entry of entries) {
			const { width, height } = entry.contentRect;
			if (width === 0 || height === 0) continue;
			camera.aspect = width / height;
			camera.updateProjectionMatrix();
			renderer?.setSize(width, height);
		}
	}

	onMount(() => {
		const width = container.clientWidth;
		const height = container.clientHeight;

		// Scene — background handled via CSS gradient, canvas is transparent
		scene = new THREE.Scene();

		// Lights — strong ambient keeps all faces bright; two opposing directionals add subtle depth
		scene.add(new THREE.AmbientLight(0xffffff, 3.0));
		const key = new THREE.DirectionalLight(0xffffff, 1.0);
		key.position.set(5, 10, 7);
		scene.add(key);
		const back = new THREE.DirectionalLight(0xffffff, 0.8);
		back.position.set(-5, -5, -7);
		scene.add(back);

		// Camera
		camera = new THREE.PerspectiveCamera(45, width / height, 0.01, 10000);
		camera.position.set(5, 3, 5);

		// Renderer
		renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true });
		renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
		renderer.setSize(width, height);
		renderer.shadowMap.enabled = true;
		container.appendChild(renderer.domElement);

		// Controls
		controls = new OrbitControls(camera, renderer.domElement);
		controls.enableDamping = true;
		controls.dampingFactor = 0.05;
		controls.screenSpacePanning = false;
		controls.minDistance = 0.1;
		controls.maxDistance = 10000;

		// Start render loop
		animate();

		// Resize observer
		const resizeObserver = new ResizeObserver(handleResize);
		resizeObserver.observe(container);

		return () => {
			cancelAnimationFrame(animId);
			resizeObserver.disconnect();
			controls.dispose();
			renderer?.dispose();
			if (renderer && container.contains(renderer.domElement)) {
				container.removeChild(renderer.domElement);
			}
			renderer = null;
		};
	});

	// Load model when status becomes ready
	$effect(() => {
		if (status === 'ready' && renderer && !modelLoaded) {
			loadModel();
		}
	});
</script>

<div class="viewer-wrapper">
	<div class="viewer-container" bind:this={container}></div>

	{#if status === 'pending' || status === 'processing'}
		<div class="viewer-overlay">
			<div class="overlay-content">
				<div class="spinner"></div>
				<p class="overlay-message">
					{status === 'pending' ? 'Queued for conversion...' : 'Converting to 3D viewer...'}
				</p>
			</div>
		</div>
	{:else if status === 'error'}
		<div class="viewer-overlay viewer-overlay-error">
			<div class="overlay-content">
				<svg width="32" height="32" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
					<circle cx="12" cy="12" r="10"/>
					<line x1="12" y1="8" x2="12" y2="12"/>
					<line x1="12" y1="16" x2="12.01" y2="16"/>
				</svg>
				<p class="overlay-message error-message">Failed to convert 3D file</p>
			</div>
		</div>
	{/if}
</div>

<style>
	.viewer-wrapper {
		position: relative;
		width: 100%;
		height: 600px;
		border-radius: var(--radius);
		overflow: hidden;
		border: 1px solid var(--color-border);
		background: linear-gradient(to bottom, #1e1e32, #0d0d1a);
	}

	:global([data-theme="light"]) .viewer-wrapper {
		background: linear-gradient(to bottom, #f2f2f2, #c0c0c0);
	}

	:global([data-theme="light"]) .viewer-overlay {
		background-color: rgba(230, 230, 230, 0.85);
	}

	:global([data-theme="light"]) .viewer-overlay-error {
		background-color: rgba(230, 230, 230, 0.9);
	}

	.viewer-container {
		width: 100%;
		height: 100%;
	}

	.viewer-container :global(canvas) {
		display: block;
		outline: none;
	}

	.viewer-overlay {
		position: absolute;
		inset: 0;
		display: flex;
		align-items: center;
		justify-content: center;
		background-color: rgba(26, 26, 46, 0.85);
		backdrop-filter: blur(2px);
	}

	.viewer-overlay-error {
		background-color: rgba(26, 26, 46, 0.9);
	}

	.overlay-content {
		display: flex;
		flex-direction: column;
		align-items: center;
		gap: 16px;
		text-align: center;
		padding: 32px;
	}

	.overlay-message {
		font-size: 14px;
		color: var(--color-text-muted);
		margin: 0;
	}

	.error-message {
		color: var(--color-danger);
	}

	.spinner {
		width: 32px;
		height: 32px;
		border: 3px solid rgba(47, 129, 247, 0.2);
		border-top-color: var(--color-accent);
		border-radius: 50%;
		animation: spin 0.8s linear infinite;
	}

	@keyframes spin {
		to { transform: rotate(360deg); }
	}
</style>

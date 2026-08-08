<script lang="ts">
	import { onMount } from 'svelte';
	import * as THREE from 'three';
	import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';
	import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
	import { getGlbUrl, getGlbStatus } from '$lib/api';

	interface LinkRef {
		file: string;
		px: number; py: number; pz: number;
		qw: number; qx: number; qy: number; qz: number;
	}

	interface Props {
		glbUrl: string;
		status: 'pending' | 'processing' | 'ready' | 'error';
		// When true, the model is rotated so the data's Z axis renders as "up"
		// (the common convention for STEP/FreeCAD data) instead of Three.js's
		// default Y-up.
		zUp?: boolean;
		// Repo-relative path of the file currently open in the blob viewer —
		// tags the root part so clicks on its own geometry resolve to it.
		currentFilePath: string;
		// Needed to fetch and compose linked FCStd files' own (independently
		// converted and cached) GLBs client-side.
		user: string;
		repo: string;
		sha: string;
		// When true, FreeCAD App::Link references embedded in the GLB's
		// asset.extras.g4c_links are fetched and merged into the scene.
		resolveLinks?: boolean;
		// Fired on a plain click on a part — the repo-relative path of the
		// file that defines the clicked geometry.
		onSelectPart?: (path: string) => void;
		// Fired on a double-click on a part — same path, but signals "navigate
		// there and view it on its own."
		onOpenPart?: (path: string) => void;
	}

	let {
		glbUrl,
		status,
		zUp = false,
		currentFilePath,
		user,
		repo,
		sha,
		resolveLinks = false,
		onSelectPart,
		onOpenPart
	}: Props = $props();

	let container: HTMLDivElement;
	let renderer: THREE.WebGLRenderer | null = null;
	let scene: THREE.Scene;
	let camera: THREE.PerspectiveCamera;
	let controls: OrbitControls;
	let animId: number;
	let modelPivot: THREE.Group;
	let currentModel: THREE.Object3D | null = null;
	let selectionHelper: THREE.BoxHelper | null = null;
	// URL of the model currently shown (or in flight) — lets us detect a new glbUrl and reload
	let loadedUrl = $state('');
	// resolveLinks value the current/in-flight load used — resolveLinks often
	// arrives asynchronously (it comes from the repo's settings, fetched
	// separately from the GLB itself), so it can flip true *after* the model
	// already loaded without links. Tracking it lets the reload effect below
	// catch that and re-load with the links merged in.
	let loadedResolveLinks = $state(false);

	function disposeObject3D(object: THREE.Object3D): void {
		object.traverse((child: THREE.Object3D) => {
			if (child instanceof THREE.Mesh) {
				child.geometry?.dispose();
				const materials = Array.isArray(child.material) ? child.material : [child.material];
				for (const material of materials) material?.dispose();
			}
		});
	}

	function setHighlight(root: THREE.Object3D | null) {
		if (selectionHelper) {
			scene.remove(selectionHelper);
			selectionHelper.dispose();
			selectionHelper = null;
		}
		if (root) {
			selectionHelper = new THREE.BoxHelper(root, 0x2f81f7);
			scene.add(selectionHelper);
		}
	}

	// Reads the App::Link references a converted FCStd's GLB carries as
	// asset.extras.g4c_links (a JSON string — see server/src/cad/GltfWriter).
	function parseLinks(gltf: any): LinkRef[] {
		const raw = gltf?.asset?.extras?.g4c_links;
		if (!raw) return [];
		try {
			const parsed = JSON.parse(raw);
			return Array.isArray(parsed) ? parsed : [];
		} catch {
			return [];
		}
	}

	function applyLinkPlacement(obj: THREE.Object3D, link: LinkRef) {
		obj.position.set(link.px, link.py, link.pz);
		obj.quaternion.set(link.qx, link.qy, link.qz, link.qw);
	}

	function loadGltf(url: string): Promise<any> {
		const loader = new GLTFLoader();
		return new Promise((resolve, reject) => {
			loader.load(url, resolve, undefined, reject);
		});
	}

	// Polls a linked file's own conversion status — GET .../glb/:sha/:path/status
	// also lazily (re-)enqueues conversion server-side if it was never seen,
	// same as the primary file, so this doesn't need to trigger anything itself.
	async function waitForGlbReady(path: string): Promise<boolean> {
		for (let attempt = 0; attempt < 60; attempt++) {
			try {
				const result = await getGlbStatus(user, repo, sha, path);
				if (result.status === 'ready') return true;
				if (result.status === 'error') return false;
			} catch {
				// keep trying
			}
			await new Promise((r) => setTimeout(r, 1500));
		}
		return false;
	}

	// Fetches each linked file's own already-converted GLB and composes it
	// into the scene under `parentGroup`, positioned by the link's placement —
	// no server-side re-conversion of linked geometry involved. Recurses for
	// links-of-links, guarding against cycles.
	async function loadLinkedParts(
		parentGroup: THREE.Object3D,
		links: LinkRef[],
		visited: Set<string>
	): Promise<void> {
		await Promise.all(
			links.map(async (link) => {
				if (visited.has(link.file)) return; // cyclic reference — skip
				const childVisited = new Set(visited);
				childVisited.add(link.file);

				try {
					const ready = await waitForGlbReady(link.file);
					if (!ready) return;

					const childUrl = getGlbUrl(user, repo, sha, link.file);
					const childGltf = await loadGltf(childUrl);

					const childGroup = new THREE.Group();
					childGroup.userData.g4cPartPath = link.file;
					childGroup.add(childGltf.scene);
					applyLinkPlacement(childGroup, link);
					parentGroup.add(childGroup);

					const nestedLinks = parseLinks(childGltf);
					if (nestedLinks.length > 0) {
						await loadLinkedParts(childGroup, nestedLinks, childVisited);
					}
				} catch (err) {
					console.error('Failed to load linked part', link.file, err);
				}
			})
		);
	}

	// Walks up from the clicked mesh to the nearest tagged wrapper group —
	// each part (the root file, and each merged linked file) is wrapped in
	// its own group tagged with its file path, so the whole part is one
	// selectable unit regardless of how many faces/sub-shapes it contains.
	function resolvePart(hit: THREE.Object3D): { path: string; root: THREE.Object3D } | null {
		let node: THREE.Object3D | null = hit;
		while (node) {
			if (node.userData?.g4cPartPath) {
				return { path: node.userData.g4cPartPath, root: node };
			}
			node = node.parent;
		}
		return null;
	}

	function pickPart(clientX: number, clientY: number): { path: string; root: THREE.Object3D } | null {
		if (!renderer || !camera || !modelPivot) return null;
		const rect = renderer.domElement.getBoundingClientRect();
		const ndc = new THREE.Vector2(
			((clientX - rect.left) / rect.width) * 2 - 1,
			-((clientY - rect.top) / rect.height) * 2 + 1
		);
		const raycaster = new THREE.Raycaster();
		raycaster.setFromCamera(ndc, camera);
		const hits = raycaster.intersectObject(modelPivot, true);
		if (hits.length === 0) return null;
		return resolvePart(hits[0].object);
	}

	// Distinguishes an actual click from a drag-to-orbit — OrbitControls
	// fires no click event of its own, but the browser's native click still
	// fires after a drag-release, so we track pointer travel ourselves.
	let pointerDownPos: { x: number; y: number } | null = null;
	const kClickDragThreshold = 5;

	function handlePointerDown(e: PointerEvent) {
		pointerDownPos = { x: e.clientX, y: e.clientY };
	}

	function handlePointerUp(e: PointerEvent) {
		const start = pointerDownPos;
		pointerDownPos = null;
		if (!start) return;
		if (Math.hypot(e.clientX - start.x, e.clientY - start.y) > kClickDragThreshold) return;

		const part = pickPart(e.clientX, e.clientY);
		setHighlight(part?.root ?? null);
		if (part && onSelectPart) onSelectPart(part.path);
	}

	function handleDoubleClick(e: MouseEvent) {
		const part = pickPart(e.clientX, e.clientY);
		if (part && onOpenPart) onOpenPart(part.path);
	}

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
		const targetUrl = glbUrl;
		const targetResolveLinks = resolveLinks;
		loadedUrl = targetUrl;
		loadedResolveLinks = targetResolveLinks;

		try {
			const gltf = await loadGltf(targetUrl);
			if (targetUrl !== glbUrl) return; // glbUrl moved on again while this load was in flight

			setHighlight(null);
			if (currentModel) {
				modelPivot.remove(currentModel);
				disposeObject3D(currentModel);
			}

			const rootGroup = new THREE.Group();
			rootGroup.userData.g4cPartPath = currentFilePath;
			rootGroup.add(gltf.scene);

			if (targetResolveLinks) {
				const links = parseLinks(gltf);
				if (links.length > 0) {
					await loadLinkedParts(rootGroup, links, new Set([currentFilePath]));
					if (targetUrl !== glbUrl || targetResolveLinks !== resolveLinks) return; // moved on while linked parts were loading
				}
			}

			modelPivot.add(rootGroup);
			currentModel = rootGroup;
			fitCameraToObject(modelPivot);
		} catch (err) {
			console.error('Failed to load GLB:', err);
			if (targetUrl === glbUrl) {
				loadedUrl = '';
				loadedResolveLinks = false;
			}
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

		// Pivot the loaded model is parented to, so the up-axis rotation stays
		// independent of the model's own geometry/transform.
		modelPivot = new THREE.Group();
		modelPivot.rotation.x = zUp ? -Math.PI / 2 : 0;
		scene.add(modelPivot);

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
			setHighlight(null);
			if (currentModel) disposeObject3D(currentModel);
			renderer?.dispose();
			if (renderer && container.contains(renderer.domElement)) {
				container.removeChild(renderer.domElement);
			}
			renderer = null;
		};
	});

	// (Re)load whenever the status becomes ready for a URL/resolveLinks
	// combination we haven't loaded yet — covers the first load, switching to
	// a different file's model, and resolveLinks arriving asynchronously
	// (from repo settings) after the initial load already happened.
	$effect(() => {
		if (
			status === 'ready' &&
			renderer &&
			glbUrl &&
			(glbUrl !== loadedUrl || resolveLinks !== loadedResolveLinks)
		) {
			loadModel();
		}
	});

	// Re-orient (and re-frame) the model whenever the up-axis setting changes.
	$effect(() => {
		if (!modelPivot) return;
		modelPivot.rotation.x = zUp ? -Math.PI / 2 : 0;
		if (currentModel) fitCameraToObject(modelPivot);
	});
</script>

<div class="viewer-wrapper">
	<div
		class="viewer-container"
		bind:this={container}
		onpointerdown={handlePointerDown}
		onpointerup={handlePointerUp}
		ondblclick={handleDoubleClick}
		role="application"
		aria-label="3D model viewer"
	></div>

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
